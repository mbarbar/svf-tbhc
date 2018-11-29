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

void RapidTypeAnalysis::initialize(SVFModule svfModule) {
    chg = new CHGraph(svfModule);
    chg->buildCHG();
    PointerAnalysis::initialize(svfModule);
}

void RapidTypeAnalysis::finalize() {
    PointerAnalysis::finalize();
    if (printStat())
        dumpRTAStats();
}

void RapidTypeAnalysis::callGraphSolveBasedOnRTA(PTACallGraph *chaCallGraph, CallEdgeMap& newEdges) {

}

void dumpRTAStats() {

}

bool RapidTypeAnalysis::hasLiveClass(const CallSite *cs) {
    if (!cppUtil::isVirtualCallSite(*cs)) return false;

    const Function *calledFunction = cs->getCalledFunction();
    struct cppUtil::DemangledName demangledName = cppUtil::demangle(calledFunction->getName());
    std::string className = demangledName.className;
    CHGraph::CHNodeSetTy descendants = chg->getDescendants(className);

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
            if (!cppUtil::isVirtualCallSite(cs) || (cppUtil::isVirtualCallSite(cs) && hasLiveClass(&cs))) {
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
    const Function *calledFunction = cs->getCalledFunction();
    struct cppUtil::DemangledName demangledName = cppUtil::demangle(calledFunction->getName());
    std::string className = demangledName.className;

    // Descendants are the possible classes.
    CHGraph::CHNodeSetTy descendants = chg->getDescendants(className);

    // Obviously, the set of possible classes includes the static type as well.
    classToVCallsMap[className].insert(cs);
    for (auto descendantI = descendants.begin(); descendantI != descendants.end(); ++descendantI) {
        classToVCallsMap[(*descendantI)->getName()].insert(cs);
    }
}

