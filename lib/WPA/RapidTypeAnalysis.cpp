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

void RapidTypeAnalysis::callGraphSolveBasedOnRTA(const PointerAnalysis::CallSiteToFunPtrMap& callsites) {
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

            CallEdgeMap newEdges;
            connectVCallToVFns(cs, virtualFunctions, newEdges);
        }
    }
}

void RapidTypeAnalysis::dumpRTAStats() {

}

void RapidTypeAnalysis::iterativeRTA(SVFModule svfModule) {
    RTAWorklist worklist;

    const Function *entry = SVFUtil::getProgEntryFunction(svfModule);
    if (entry != NULL) {
        // TODO: main hardcoded; is that okay?
        worklist.push(entry);
    } else {
        // There is no entry, we're probably dealing with a library.
        for (auto funI = svfModule.begin(); funI != svfModule.end(); ++funI) {
            worklist.push(*funI);
        }
    }

    while (!worklist.empty()) {
        const Function *fun = worklist.front();
        worklist.pop();

        // Don't reanalyse.
        if (liveFunctions.find(fun) != liveFunctions.end()) continue;
        liveFunctions.insert(fun);

        for (auto bbI = fun->begin(); bbI != fun->end(); ++bbI) {
            for (auto instI = bbI->begin(); instI != bbI->end(); ++instI) {
                // Only callsites matter.
                if (!SVFUtil::isNonInstricCallSite(&(*instI))) continue;
                const CallSite cs = SVFUtil::getLLVMCallSite(&(*instI));

                // TODO: function pointer calls unconsidered...
                const Function *callee = SVFUtil::getCallee(cs);
                if (SVFUtil::getCallee(cs) != NULL) {
                    if (cppUtil::isConstructor(callee)) {
                        // Constructor call.
                        handleConstructorCall(&cs, worklist);
                    } else {
                        // Direct call.
                        handleDirectCall(&cs, worklist);
                    }
                } else if (cppUtil::isVirtualCallSite(cs)) {
                    // Virtual call.
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
    getVFnsFromCHA(*cs, chaVFns);

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
    const Function *calledConstructor = SVFUtil::getCallee(*cs);
    cppUtil::DemangledName demangledName = cppUtil::demangle(calledConstructor->getName());
    if (!isBaseConstructorCall(cs)) {
        // Class is being explicitly built by the programmer.
        instantiateClass(demangledName.className, worklist);
    }

    worklist.push(calledConstructor);
}

void RapidTypeAnalysis::handleDirectCall(const CallSite *cs, RTAWorklist &worklist) {
    worklist.push(SVFUtil::getCallee(*cs));
}

void RapidTypeAnalysis::instantiateClass(const std::string className, RTAWorklist &worklist) {
    // Class already instantiated.
    if (liveClasses.find(className) != liveClasses.end()) return;
    // Set as live.
    liveClasses.insert(className);

    // Analyse the functions we missed due to className having been dead.
    if (deadClassToVfnsMap.find(className) != deadClassToVfnsMap.end()) {
        std::set<const Function *> vfns = deadClassToVfnsMap.at(className);

        for (auto vfnI = vfns.begin(); vfnI != vfns.end(); ++vfnI) {
            worklist.push(*vfnI);
        }

        deadClassToVfnsMap.erase(className);
    }
}

