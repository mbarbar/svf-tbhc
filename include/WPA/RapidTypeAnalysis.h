//===- RapidTypeAnalysis.h -- RTA for call graph construction ------------//

/*
 * RapidTypeAnalysis.h
 *
 *  Created on: 22 Nov. 2018
 *      Author: Mohamad Barbar
 */

#ifndef INCLUDE_WPA_RAPIDTYPEANALYSIS_H_
#define INCLUDE_WPA_RAPIDTYPEANALYSIS_H_

#include <queue>

#include "MemoryModel/PointerAnalysis.h"
#include "WPA/Andersen.h"

class RapidTypeAnalysis: public Andersen {
public:
    typedef std::queue<const Function *> RTAWorklist;

private:
    std::set<const Function *> liveFunctions;
    std::set<std::string> liveClasses;

    /// Maps currently dead classes to all the vfns belonging to it that we have
    /// come across (according to CHA). This is to analyse them once that class
    /// is instantiated.
    std::map<const std::string, std::set<const Function *>> deadClassToVfnsMap;

public:
    /// Constructor
    RapidTypeAnalysis(PTATY type = RapidTypeCPP_WPA)
        : Andersen(type) {
    }

    /// Destructor
    virtual ~RapidTypeAnalysis() {
    }

    /// Initialize analysis
    virtual inline void initialize(SVFModule svfModule) {
        PointerAnalysis::initialize(svfModule);
    }

    /// Rapid Type Analysis
    virtual inline void analyze(SVFModule svfModule) {
        initialize(svfModule);
        iterativeRTA(svfModule);
        callGraphSolveBasedOnRTA(getIndirectCallsites());
        finalize();
    }

    /// Finalize analysis
    virtual inline void finalize() {
        PointerAnalysis::finalize();
        dumpRTAStats();
    }

    /// Resolve callgraph based on CHA
    void callGraphSolveBasedOnRTA(const PointerAnalysis::CallSiteToFunPtrMap& callsites);

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
    /// Methods for iterative RTA algorithm.
    //@{
    /// Entry to iterative RTA.
    void iterativeRTA(SVFModule svfModule);

    /// Handle direct callsites, adding the function to the worklist.
    void handleDirectCall(const CallSite *cs, RTAWorklist &worklist);

    /// Handle virtual callsites, adding what is necessary to the worklist.
    void handleVirtualCall(const CallSite *cs, RTAWorklist &worklist);

    /// Handle constructors, setting a class as live if necessary.
    void handleConstructorCall(const CallSite *cs, RTAWorklist &worklist);

    /// Set class as live.
    void instantiateClass(const std::string className, RTAWorklist &worklist);

    /// Returns true if a constructor call is used to build the base object of another.
    static bool isBaseConstructorCall(const CallSite *cs) {
        return false;
        // TODO.
    }
    //@}
};


#endif /* INCLUDE_WPA_RAPIDTYPEANALYSIS_H_ */
