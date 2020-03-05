//===- FlowSensitiveContextSensitive.h -- flow-sensitive context sensitive PTA ------//

/*
 * FlowSensitiveContextSensitive.h
 *
 *  Created on: Mar 05, 2020
 *      Author: Mohamad Barbar
 */

#ifndef FLOWSENSITIVECONTEXTSENSITIVE_H_
#define FLOWSENSITIVECONTEXTSENSITIVE_H_

#include "WPA/FlowSensitive.h"
class SVFModule;

/*!
 * Flow-sensitive + context-sensitive whole program pointer analysis.
 */
class FlowSensitiveContextSensitive : public FlowSensitive {
public:
    /// Constructor
    FlowSensitiveContextSensitive(PTATY type = FSCS_WPA);

    /// Flow sensitive analysis with context-sensitivity.
    virtual void analyze(SVFModule svfModule) override;
    /// Initialize analysis.
    virtual void initialize(SVFModule svfModule) override;
    /// Finalize analysis.
    virtual void finalize() override;

    /// Get PTA name
    virtual const std::string PTAName() const override{
        return "FSCS";
    }
};

#endif /* FLOWSENSITIVECONTEXTSENSITIVE_H_ */
