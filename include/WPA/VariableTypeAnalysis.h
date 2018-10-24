/*
 * VariableTypeAnalysis.h
 *
 *  Created on: 24/10/2018
 *      Author: Mohamad Barbar
 */

#include "Util/SVFModule.h"
#include "MemoryModel/PointerAnalysis.h"

#ifndef INCLUDE_VARIABLETYPEANALYSIS_H_
#define INCLUDE_VARIABLETYPEANALYSIS_H_

class VariableTypeAnalysis : public PointerAnalysis {
public:
    /// Constructor
    VariableTypeAnalysis(PTATY ty = PTATY::VariableTypeCPP_WPA)
                        : PointerAnalysis(ty) {
    }

    /// Destructor
    virtual ~VariableTypeAnalysis() {
    }

    /// Variable type analysis
    void analyze(SVFModule svfModule);

    /// Initialize analysis
    void initialize(SVFModule svfModule);

    /// Finalize analysis
    virtual inline void finalize();
};

#endif /* INCLUDE_VARIABLETYPEANALYSIS_H_ */
