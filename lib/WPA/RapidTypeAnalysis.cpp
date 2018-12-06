//===- RapidTypeAnalysis.cpp -- Implementation of RTA class -------------//

/*
 * RapidTypeAnalysis.cpp
 *
 *  Created on: 22 Sep. 2018
 *      Author: Mohamad Barbar
 */

#include "Util/CPPUtil.h"
#include "Util/SVFUtil.h"
#include "MemoryModel/CHA.h"
#include "WPA/RapidTypeAnalysis.h"

void RapidTypeAnalysis::callGraphSolveBasedOnRTA(const PointerAnalysis::CallSiteToFunPtrMap& callsites, PointerAnalysis::CallEdgeMap& newEdges) {
    for (auto csFnI = callsites.begin(); csFnI != callsites.end(); ++csFnI) {
        CallSite cs = csFnI->first;

        CHGraph::CHNodeSetTy possibleClasses;
        if (cppUtil::isVirtualCallSite(cs)) {
            const CHGraph::CHNodeSetTy &chaClasses = chgraph->getCSClasses(cs);

            // Intersect the CHA result with that of RTA.
            for (auto chaClassI = chaClasses.begin(); chaClassI != chaClasses.end(); ++chaClassI) {
                if (liveClasses.find((*chaClassI)->getName()) != liveClasses.end()) {
                    possibleClasses.insert(*chaClassI);
                }
            }

            CHGraph::VTableSet vtbls;
            for (auto possibleClassI = possibleClasses.begin(); possibleClassI != possibleClasses.end(); ++possibleClassI) {
                const GlobalValue *vtbl = (*possibleClassI)->getVTable();
                if (vtbl != NULL) vtbls.insert(vtbl);
            }

            VFunSet virtualFunctions;
            if (vtbls.size() > 0) {
                chgraph->getVFnsFromVtbls(cs, vtbls, virtualFunctions);
            } else {
                // TODO: this shouldn't happen?
                return;
            }

            connectVCallToVFns(cs, virtualFunctions, newEdges);
        }
    }
}

void RapidTypeAnalysis::dumpRTAStats() {

}

bool RapidTypeAnalysis::hasLiveClass(const CallSite *cs) {
    if (!cppUtil::isVirtualCallSite(*cs)) return false;

    const Function *calledFunction = cs->getCalledFunction();
    std::string funName = cppUtil::getFunNameOfVCallSite(*cs);
    struct cppUtil::DemangledName demangledName = cppUtil::demangle(funName);
    std::string className = demangledName.className;
    CHGraph::CHNodeSetTy descendants = chgraph->getDescendants(className);

    if (liveClasses.find(className) != liveClasses.end()) return true;
    for (auto descendantI = descendants.begin(); descendantI != descendants.end(); ++descendantI) {
        if (liveClasses.find((*descendantI)->getName()) != liveClasses.end()) return true;
    }

    return false;
}

void RapidTypeAnalysis::performRTA(SVFModule svfModule) {
     analyzeFunction(svfModule.getFunction("main"), false);
}

void RapidTypeAnalysis::analyzeFunction(const Function *fun, bool isBase) {
    if (cppUtil::isConstructor(fun) && !isBase) {
        // *Programmer* is explicitly constructing an object.
        struct cppUtil::DemangledName demangledName = cppUtil::demangle(fun->getName());
        instantiate(demangledName.className);
    }

    if (liveFunctions.find(fun) != liveFunctions.end()) {
        // Already analyzed.
        return;
    }

    liveFunctions.insert(fun);

    for (auto bbI = fun->begin(); bbI != fun->end(); ++bbI) {
        for (auto instI = bbI->begin(); instI != bbI->end(); ++instI) {
            // Only callsites matter.
            if (!SVFUtil::isa<CallInst>(*instI)) continue;
            const CallSite cs = SVFUtil::getLLVMCallSite(&(*instI));

            // TODO: function calls unconsidered...
            if ((!cppUtil::isVirtualCallSite(cs) && !cs.isIndirectCall()) || (cppUtil::isVirtualCallSite(cs) && hasLiveClass(&cs))) {
                addCall(&cs);
            } else {
                addVirtualMappings(&cs);
            }
        }
    }
}

void RapidTypeAnalysis::addCall(const CallSite *cs) {
    liveCallsites.insert(cs);
    analyzeFunction(cs->getCalledFunction(), isBaseConstructorCall(cs));
}

void RapidTypeAnalysis::instantiate(const std::string className) {
    if (liveClasses.find(className) != liveClasses.end()) {
        // Class already instantiated; already live.
        return;
    }

    // Set the class as live.
    liveClasses.insert(className);

    // Check if there are callsites to liven.
    auto callsitesI = classToVCallsMap.find(className);
    if (callsitesI == classToVCallsMap.end()) {
        // Nothing to do, no callsites to be resolved.
        return;
    }

    std::set<const CallSite *> callsites = callsitesI->second;

    std::set<const CallSite *> callsitesToRemove;
    for (auto csI = callsites.begin(); csI != callsites.end(); ++csI) {
        if (liveCallsites.find(*csI) != liveCallsites.end()) {
            addCall(*csI);
            callsitesToRemove.insert(*csI);
        }
    }

    // Remove callsites set to live for this class.
    for (auto csI = callsitesToRemove.begin(); csI != callsitesToRemove.end(); ++csI) {
        callsites.erase(*csI);
    }
}

void RapidTypeAnalysis::addVirtualMappings(const CallSite *cs) {
    std::string funName = cppUtil::getFunNameOfVCallSite(*cs);
    struct cppUtil::DemangledName demangledName = cppUtil::demangle(funName);
    std::string className = demangledName.className;

    // Descendants are the possible classes.
    CHGraph::CHNodeSetTy descendants = chgraph->getDescendants(className);

    // Obviously, the set of possible classes includes the static type as well.
    classToVCallsMap[className].insert(cs);
    for (auto descendantI = descendants.begin(); descendantI != descendants.end(); ++descendantI) {
        classToVCallsMap[(*descendantI)->getName()].insert(cs);
    }
}

void RapidTypeAnalysis::iterativeRTA(SVFModule svfModule) {
    RTAWorklist worklist;
    // TODO: main hardcoded; want to add all the roots in the callgraph.
    worklist.push(svfModule.getFunction("main"));

    while (!worklist.empty()) {
        const Function *fun = worklist.front();
        worklist.pop();

        // Don't reanalyse.
        if (liveFunctions.find(fun) != liveFunctions.end()) return;
        liveFunctions.insert(fun);

        for (auto bbI = fun->begin(); bbI != fun->end(); ++bbI) {
            for (auto instI = bbI->begin(); instI != bbI->end(); ++instI) {
                // Only callsites matter.
                if (!SVFUtil::isNonInstricCallSite(&(*instI))) continue;
                const CallSite cs = SVFUtil::getLLVMCallSite(&(*instI));

                // TODO: function pointer calls unconsidered...
                const Function *calledFunction = cs.getCalledFunction();
                if (calledFunction != NULL && cppUtil::isConstructor(calledFunction)) {
                    handleConstructorCall(&cs, worklist);
                } else if (!cppUtil::isVirtualCallSite(cs) && !cs.isIndirectCall()) {
                    // Direct call.
                    worklist.push(cs.getCalledFunction());
                } else if (cppUtil::isVirtualCallSite(cs)) {
                    handleVirtualCall(&cs, worklist);
                } else {
                    // TODO.
                }
            }
        }
    }
}

void RapidTypeAnalysis::handleVirtualCall(const CallSite *cs, RTAWorklist &worklist) {
    VFunSet chaVFns;
    VTableSet chaVtbls = chgraph->getCSVtblsBasedonCHA(*cs);
    chgraph->getVFnsFromVtbls(*cs, chaVtbls, chaVFns);

    for (auto vfn = chaVFns.begin(); vfn != chaVFns.end(); ++vfn) {
        cppUtil::DemangledName demangledName = cppUtil::demangle((*vfn)->getName());
        if (liveClasses.find(demangledName.className) != liveClasses.end()) {
            // Class is live so analyse the virtual function.
            worklist.push(*vfn);
        } else {
            // It's not live, but it might be later.
            deadClassToVfnsMap[demangledName.className].insert(*vfn);
        }
    }
}

void RapidTypeAnalysis::handleConstructorCall(const CallSite *cs, RTAWorklist &worklist) {
    const Function *calledConstructor = cs->getCalledFunction();
    cppUtil::DemangledName demangledName = cppUtil::demangle(calledConstructor->getName());
    if (!isBaseConstructorCall(cs)) {
        // Class is being explicitly built by the programmer.
        liveClasses.insert(demangledName.className);
    }

    worklist.push(calledConstructor);
}

