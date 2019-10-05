//===----- CHG.h -- Base class of CHG implementations ------------------------//
// A common base to CHGraph and DCHGraph.

/*
 * CHG.h
 *
 *  Created on: Aug 24, 2019
 *      Author: Mohamad Barbar
 */

#ifndef CHG_H_
#define CHG_H_

typedef std::set<const GlobalValue*> VTableSet;
typedef std::set<const Function*> VFunSet;

/// Common base for class hierarchy graph. Only implements what PointerAnalysis needs.
class CommonCHGraph {
public:
    virtual const bool csHasVFnsBasedonCHA(CallSite cs) const = 0;
    virtual const VFunSet &getCSVFsBasedonCHA(CallSite cs) const = 0;
    virtual const bool csHasVtblsBasedonCHA(CallSite cs) const = 0;
    virtual const VTableSet &getCSVtblsBasedonCHA(CallSite cs) const = 0;
    virtual void getVFnsFromVtbls(CallSite cs, VTableSet &vtbls, VFunSet &virtualFunctions) const = 0;
};

#endif /* CHG_H_ */
