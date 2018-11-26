//===- RapidTypeAnalysis.h -- RTA for call graph construction ------------//

/*
 * RapidTypeAnalysis.h
 *
 *  Created on: 22 Nov. 2018
 *      Author: Mohamad Barbar
 */

#ifndef INCLUDE_WPA_RAPIDTYPEANALYSIS_H_
#define INCLUDE_WPA_RAPIDTYPEANALYSIS_H_

#include "MemoryModel/PointerAnalysis.h"

class RapidTypeAnalysis:  public BVDataPTAImpl{
public:
    /// Constructor
    RapidTypeAnalysis(PTATY type = RapidTypeCPP_WPA)
        :  BVDataPTAImpl(type){
    }

    /// Destructor
    virtual ~RapidTypeAnalysis() {
    }

    /// Rapid Type Analysis
    void analyze(SVFModule svfModule);

    /// Initialize analysis
    void initialize(SVFModule svfModule);

    /// Finalize analysis
    virtual inline void finalize();

    /// Resolve callgraph based on CHA
    void callGraphSolveBasedOnRTA(PTACallGraph *chaCallGraph, CallEdgeMap& newEdges);

    /// Statistics of RTA and callgraph
    void dumpRTAStats();

    /// Methods for support type inquiry through isa, cast, and dyn_cast:
    //@{
    static inline bool classof(const RapidTypeAnalysis *) {
        return true;
    }
    static inline bool classof(const PointerAnalysis *pta) {
        return pta->getAnalysisTy() == RapidTypeCPP_WPA;
    }
    //@}
};


#endif /* INCLUDE_WPA_RAPIDTYPEANALYSIS_H_ */
