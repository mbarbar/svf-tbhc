//===- VTGraph.h -- Offline constraint graph -----------------------------//

/*
 * VTGraph.h
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#ifndef VTGRAPH_H
#define VTGRAPH_H

#include <tuple>

#include "MemoryModel/OfflineConsG.h"
#include "MemoryModel/CHA.h"
#include "MemoryModel/PointerAnalysis.h"

/*!
 * Offline constraint graph for Andersen's analysis.
 * In OCG, a 'ref' node is used to represent the point-to set of a constraint node.
 * 'Nor' means a constraint node of its corresponding ref node.
 */
class VTGraph: public ConstraintGraph {
private:
    // What prefixes every class (type) name in LLVM.
    static const std::string CLASS_NAME_PREFIX;

    // Maps types to the new nodes generated to replace
    // all the memory objects of that type.
    std::map<const Type *, NodeID> typeToNode;

    // Maps a field (declarer + offset) to the node which
    // will represent that field.
    std::map<std::tuple<std::string, u32_t>, NodeID> fieldRepresentationMap;

    // For some queries
    CHGraph *chg;

public:
    VTGraph(PAG *p, SVFModule svfModule, bool retainScalars, bool fieldBased) : ConstraintGraph(p) {
        chg = PointerAnalysis::getCHGraph();
        collapseMemoryObjectsIntoTypeObjects(retainScalars);
        if (fieldBased) collapseFields();
    }

    // Replaces memory objects with type objects. If retainScalars is false,
    // scalar typed objects will be removed entirely.
    void collapseMemoryObjectsIntoTypeObjects(bool retainScalars);

    // Collapses all fields to a single node.
    // Nodes referring to field f collapse to node X::f
    // where X is the class declaring f.
    void collapseFields(void);

    NodeID findTypeObjForType(const Type *type);

    /// Dump the VT graph.
    virtual void dump(std::string name) override;

    /// Get a field of a memory object
    virtual inline NodeID getGepObjNode(NodeID id, const LocationSet& ls);

    /// Get a field-insensitive node of a memory object
    virtual inline NodeID getFIObjNode(NodeID id);

    static std::string getClassNameFromPointerType(const Type *type);
    static std::string getClassNameFromStructType(const StructType *structType);
    static std::string getClassNameFromType(const Type *type);
    static const Type *dereferencePointerType(const PointerType *pt);

    /// Returns true if type is a non scalar type or a non-class struct (i.e. not
    /// in class hierarchy) which has no non-scalar types (recursively). Conservative.
    static bool hasNonScalarTypes(const Type *type);
private:

    std::string getFieldDeclarer(std::string accessingClass, const StructType *ptrType, u32_t fieldOffset);
};

namespace llvm {
/* !
 * GraphTraits specializations for the generic graph algorithms.
 * Provide graph traits for traversing from a constraint node using standard graph traversals.
 */

template<> struct GraphTraits<VTGraph*> : public GraphTraits<GenericGraph<ConstraintNode,ConstraintEdge>* > {
typedef ConstraintNode *NodeRef;
};

}

#endif // VTGRAPH_H
