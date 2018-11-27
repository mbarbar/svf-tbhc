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
private:
    std::set<const Function *> liveFunctions;  // F_L
    std::set<const CallSite *> liveCallsites;  // S_L
    std::set<std::string> liveClasses;         // C_L
    /// Maps class names to callsite which may possibly be making calls to a
    /// method within that class.
    std::map<const std::string, const CallSite *> classToVCallMap;  // Q_V

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

private:
    /// Methods from the RTA algorithm in the thesis.
    /// http://digitalassets.lib.berkeley.edu/techreports/ucb/text/CSD-98-1017.pdf page 61.
    //@{
    /// Returns true if a constructor call is used to build the base object of another.
    static bool isBaseConstructorCall(const CallSite *cs) {
        // TODO.
    }

    /// Entry into RTA.
    void performRTA(SVFModule svfModule);
    /// Analyzes all callsites in a function.
    void analyzeFunction(const Function *fun, bool isBase);
    /// Adds a callsite to the set of live callsites, and analyzes what it calls.
    void addCall(const CallSite *cs);
    /// Marks a class as active, and adds all relevant calls.
    void instantiate(const std::string className);
    /// Maps all possible classes (i.e. which the callsite can resolve to) to the callsite.
    void addVirtualMappings(const CallSite *cs);
    //@}
};


#endif /* INCLUDE_WPA_RAPIDTYPEANALYSIS_H_ */
