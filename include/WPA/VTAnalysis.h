

#ifndef INCLUDE_WPA_VTANALYSIS_H_
#define INCLUDE_WPA_VTANALYSIS_H_

#include "WPA/Andersen.h"
#include "MemoryModel/VTGraph.h"
#include "MemoryModel/CHA.h"
#include "MemoryModel/PointerAnalysis.h"

class VTAnalysis: public AndersenWaveDiff {
private:
    bool vtaPlus;
    /// If false, scalar types will be removed entirely.
    bool retainScalars;
public:
    /// Constructor
    VTAnalysis(bool retainScalars, bool vtaPlus, PTATY type = VariableTypeCPP_WPA) {
        iterationForPrintStat = OnTheFlyIterBudgetForStat;
        this->vtaPlus = vtaPlus;
        this->retainScalars = retainScalars;
    }

    typedef SCCDetection<VTGraph*> VSCC;

    /// Initialize analysis
    virtual inline void initialize(SVFModule svfModule) {
        Type *t = NULL;

        resetData();

        /// Build PAG
        PointerAnalysis::initialize(svfModule);
        consCG = createVTGraph(svfModule);
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
        /*
        for (auto nid = pag->begin(); nid != pag->end(); ++nid) {
            PointsTo &pts = getPts(nid->first);
            for (auto it = pts.begin(); it != pts.end(); ++it) {
                NodeID id = *it;
                assert(SVFUtil::isa<TypeObjPN>(pag->getPAGNode(id)) || SVFUtil::isa<DummyObjPN>(pag->getPAGNode(id)));
            }
        }
        */
    }

    void inline setVtaPlus(bool vtaPlus) {
        this->vtaPlus = vtaPlus;
    }

    void inline setRetainScalars(bool retainScalars) {
        this->retainScalars= retainScalars;
    }

    void validateTests();

    VTGraph* createVTGraph(SVFModule svfModule);
};


#endif /* INCLUDE_WPA_VTANALYSIS_H_ */
