

#ifndef INCLUDE_WPA_VTANALYSIS_H_
#define INCLUDE_WPA_VTANALYSIS_H_

#include "WPA/Andersen.h"
#include "MemoryModel/VTGraph.h"
#include "MemoryModel/CHA.h"
#include "MemoryModel/PointerAnalysis.h"

class VTAnalysis: public Andersen {
private:
    bool vtaPlus;
public:
    /// Constructor
    VTAnalysis(PTATY type = VariableTypeCPP_WPA) {
        iterationForPrintStat = OnTheFlyIterBudgetForStat;
        this->vtaPlus = false;
    }

    typedef SCCDetection<VTGraph*> VSCC;

    /// Initialize analysis
    virtual inline void initialize(SVFModule svfModule) {
        Type *t = NULL;

        resetData();

        /// Build PAG
        PointerAnalysis::initialize(svfModule);
        createVTGraph(svfModule);
        setGraph(consCG);

        /// Create statistic class
        stat = new AndersenStat(this);

        //consCG->dump("vtg_initial");
    }

    virtual inline void analyze(SVFModule svfModule) {
        initialize(svfModule);
        processAllAddr();
        solve();
        finalize();
    }

    /// Finalize analysis
    virtual inline void finalize() {
        consCG->dump("vtg_final");
        consCG->print();

        PointerAnalysis::finalize();
        //validateTests();
    }

    void inline setVtaPlus(bool vtaPlus) {
        this->vtaPlus = vtaPlus;
    }

    void validateTests();

    VTGraph* createVTGraph(SVFModule svfModule);
};


#endif /* INCLUDE_WPA_VTANALYSIS_H_ */
