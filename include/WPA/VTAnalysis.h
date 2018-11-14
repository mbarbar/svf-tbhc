

#ifndef INCLUDE_WPA_VTANALYSIS_H_
#define INCLUDE_WPA_VTANALYSIS_H_

#include "WPA/Andersen.h"
#include "MemoryModel/VTGraph.h"

class VTAnalysis: public Andersen {

public:
    typedef SCCDetection<VTGraph*> VSCC;

    /// Initialize analysis
    virtual inline void initialize(SVFModule svfModule) {
        resetData();
        /// Build PAG
        PointerAnalysis::initialize(svfModule);
        consCG = createVTGraph();
        setGraph(consCG);
        /// Create statistic class
        stat = new AndersenStat(this);
        consCG->dump("consCG_initial");
    }

    /// Finalize analysis
    virtual inline void finalize() {
        /// dump constraint graph if PAGDotGraph flag is enabled
        consCG->dump("consCG_final");
        consCG->print();
        PointerAnalysis::finalize();
        validateTests();

    }

    void validateTests();

    VTGraph* createVTGraph();
};


#endif /* INCLUDE_WPA_VTANALYSIS_H_ */
