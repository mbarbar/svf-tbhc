//===----- DCHG.h -- CHG using DWARF debug info ---------------------------//
// This is based upon CHA.h.

/*
 * DCHG.h
 *
 *  Created on: Aug 23, 2019
 *      Author: Mohamad Barbar
 */

#ifndef DCHG_H_
#define DCHG_H_

#include "MemoryModel/CHA.h"
#include "MemoryModel/GenericGraph.h"
#include "Util/SVFModule.h"
#include "Util/WorkList.h"

class SVFModule;
class DCHNode;

class DCHEdge : CHEdge {
};

class DCHNode : CHNode {
public:
    DCHNode(DIType *diType, const std::string name, NodeID i = 0, GNodeK k = 0)
    : CHNode(name, i, k) {
        this->diType = diType;
    }

    llvm::DIType *getDIType(void) const {
        return diType;
    }

private:
    llvm::DIType *diType;
};

/// Dwarf based CHG.
class DCHGraph : public CHGraph {
public:
    virtual void buildCHG(void);

private:
    SVFModule svfMod;
    u32_t classNum;
    s32_t vfID;
    double buildingCHGTime;
    std::map<const std::string, CHNode *> classNameToNodeMap;
    NameToCHNodesMap classNameToDescendantsMap;
    NameToCHNodesMap classNameToAncestorsMap;
    NameToCHNodesMap classNameToInstAndDescsMap;
    NameToCHNodesMap templateNameToInstancesMap;
    CallSiteToCHNodesMap csToClassesMap;

    std::map<const Function*, s32_t> virtualFunctionToIDMap;
    CallSiteToVTableSetMap csToCHAVtblsMap;
    CallSiteToVFunSetMap csToCHAVFnsMap;

    // ----
    std::map<llvm::DIType *, DCHNode *> diTypeToNodeMap;

private:
    /// Construction helper to process DIBasicTypes.
    void handleDIBasicType(const DIBasicType *basicType);
    /// Construction helper to process DICompositeTypes.
    void handleDICompositeType(const DICompositeType *compositeType);
    /// Construction helper to process DIDerivedTypes.
    void handleDIDerivedType(const DIDerivedType *derivedType);
    /// Construction helper to process DISubroutineTypes.
    void handleDISubroutineType(const DISubroutineType *subroutineType);

    /// Creates a node from type, or returns it if it exists.
    /// Only suitable for TODO.
    DCHNode *getOrCreateNode(llvm::DIType *type);

    /// Creates an edge between from t1 to t2.
    DCHEdge *addEdge(llvm::DIType *t1, llvm::DIType *t2);
    /// Returns the edge between t1 and t2 if it exists, returns NULL otherwise.
    DCHEdge *hasEdge(llvm::DIType *t1, llvm::DIType *t2);
};


namespace llvm {
/* !
 * GraphTraits specializations for generic graph algorithms.
 * Provide graph traits for traversing from a constraint node using standard graph traversals.
 */
template<> struct GraphTraits<CHNode*> : public GraphTraits<GenericNode<CHNode,CHEdge>*  > {
};

/// Inverse GraphTraits specializations for call graph node, it is used for inverse traversal.
template<>
struct GraphTraits<Inverse<CHNode*> > : public GraphTraits<Inverse<GenericNode<CHNode,CHEdge>* > > {
};

template<> struct GraphTraits<CHGraph*> : public GraphTraits<GenericGraph<CHNode,CHEdge>* > {
    typedef CHNode *NodeRef;
};

}

#endif /* DCHG_H_ */

