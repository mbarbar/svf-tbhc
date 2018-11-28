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

