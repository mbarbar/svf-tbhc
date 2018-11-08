//===- VTAGraph.h -- Offline constraint graph -----------------------------//

/*
 * VTAGraph.h
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#ifndef VTAGRAPH_H
#define VTAGRAPH_H

#include "MemoryModel/OfflineConsG.h"

/*!
 * Offline constraint graph for Andersen's analysis.
 * In OCG, a 'ref' node is used to represent the point-to set of a constraint node.
 * 'Nor' means a constraint node of its corresponding ref node.
 */
class VTAGraph: public OfflineConsG {
private:
    // Maps types to the new nodes generated to replace
    // all the memory objects of that type.
    std::map<Type *, NodeID> typeToNode;

public:
    VTAGraph(PAG *p) : OfflineConsG(p) {
    }

    void removeMemoryObjectNodes(void);
};

#endif // VTAGRAPH_H
