//===- TypeBasedHeapCloning.h -- type-based Flow-sensitive heap cloning----------------//

/*
 * TypeBasedHeapCloning.h
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#ifndef TYPEBASEDHEAPCLONING_H_
#define TYPEBASEDHEAPCLONING_H_

#include "MSSA/SVFGOPT.h"
#include "MSSA/SVFGBuilder.h"
#include "WPA/FlowSensitive.h"
class SVFModule;

/*!
 * Flow sensitive whole program pointer analysis with type-based heap cloning.
 */
class TypeBasedHeapCloning : public FlowSensitive {
};

#endif /* TYPEBASEDHEAPCLONING_H_ */
