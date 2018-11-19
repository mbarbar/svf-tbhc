

#ifndef INCLUDE_WPA_VTANALYSIS_H_
#define INCLUDE_WPA_VTANALYSIS_H_

#include "WPA/Andersen.h"
#include "MemoryModel/VTGraph.h"
#include "MemoryModel/CHA.h"

class VTAnalysis: public Andersen {
public:
    typedef SCCDetection<VTGraph*> VSCC;

    /// Initialize analysis
    virtual inline void initialize(SVFModule svfModule) {
        CHGraph *chg = new CHGraph(svfModule);
        chg->buildCHG();
        Type *t = NULL;

        for (auto nodeI = chg->begin(); nodeI != chg->end(); ++nodeI) {
            llvm::outs() << "NODE: " << nodeI->second->getName() << "\n";
        }

        resetData();

        /// Build PAG
        PointerAnalysis::initialize(svfModule);

        consCG = createVTGraph(svfModule);
        setGraph(consCG);

        /// Create statistic class
        stat = new AndersenStat(this);

        //consCG->dump("vtg_initial");


        PAGNode *node = pag->getPAGNode(4);
        const Type *type = node->getType();
        const Value *value = node->getValue();
        const PointerType *ptrType = static_cast<const PointerType *>(type);
    }

    virtual inline void analyze(SVFModule svfModule) {
        initialize(svfModule);
        finalize();
    }

    /// Finalize analysis
    virtual inline void finalize() {
        consCG->dump("vtg_final");
        consCG->print();

        PointerAnalysis::finalize();
        validateTests();
    }

    void validateTests();

    VTGraph* createVTGraph(SVFModule svfModule);
};


#endif /* INCLUDE_WPA_VTANALYSIS_H_ */
