//===- RapidTypeAnalysis.cpp -- Implementation of RTA class -------------//

/*
 * RapidTypeAnalysis.cpp
 *
 *  Created on: 22 Sep. 2018
 *      Author: Mohamad Barbar
 */

#include "WPA/RapidTypeAnalysis.h"

void RapidTypeAnalysis::analyze(SVFModule svfModule) {

}

void RapidTypeAnalysis::initialize(SVFModule svfModule) {
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

void RapidTypeAnalysis::performRTA(SVFModule svfModule) {
     analyzeFunction(svfModule.getFunction("main"), false);
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
