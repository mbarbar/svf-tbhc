/*
 * VariableTypeAnalysis.h
 *
 *  Created on: 24/10/2018
 *      Author: Mohamad Barbar
 */

#include "Util/SVFModule.h"
#include "MemoryModel/PointerAnalysis.h"
#include "MemoryModel/ConsG.h"


#ifndef INCLUDE_VARIABLETYPEANALYSIS_H_
#define INCLUDE_VARIABLETYPEANALYSIS_H_

class VariableTypeAnalysis : public PointerAnalysis {
private:
    ConstraintGraph *cg;

public:
    /// Constructor
    VariableTypeAnalysis(PTATY ty = PTATY::VariableTypeCPP_WPA)
                        : PointerAnalysis(ty) {
        cg = new ConstraintGraph(pag);
    }

    /// Destructor
    virtual ~VariableTypeAnalysis() {
    }

    /// Variable type analysis
    virtual void analyze(SVFModule svfModule);

    /// Initialize analysis
    virtual void initialize(SVFModule svfModule);

    /// Finalize analysis
    virtual void finalize();
};

#endif /* INCLUDE_VARIABLETYPEANALYSIS_H_ */
