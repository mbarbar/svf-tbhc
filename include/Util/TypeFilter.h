//===- TypeFilter.h -- Type filter/type-based heap cloning base ------------//

/*
 * TypeFilter.h
 *
 * Contains data structures and functions to extend a pointer analysis
 * with type-based heap cloning/type filtering.
 *
 *  Created on: Feb 08, 2020
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/DCHG.h"
#include "MemoryModel/PAG.h"
#include "Util/BasicTypes.h"

class TypeFilter {
public:
    /// Returns raw ctir metadata of a Value. Returns null if it doesn't exist.
    static const MDNode *getRawCTirMetadata(const Value *);

protected:
    /// The undefined type (•); void.
    static const DIType *undefType;

    TypeFilter(PointerAnalysis *pta);

    /// Required by user. Handles back-propagation of newly created clone after all
    /// metadata has been set. Used by cloneObject.
    virtual void backPropagate(NodeID clone) = 0;

    /// Class hierarchy graph built from debug information. Required, CHG from
    /// IR is insufficient.
    DCHGraph *dchg = nullptr;

    /// DCHG *must* be set by extending class once the DCHG is available.
    void setDCHG(DCHGraph *dchg);
    /// PAG *must* be set by extending class once the PAG is available.
    void setPAG(PAG *pag);

    /// Check if an object is a black hole obj or a constant object. Required since
    /// other implementations obviously do not account for clones.
    bool isBlkObjOrConstantObj(NodeID o) const;

    /// Wrapper around DCHGraph::isBase. Purpose is to keep our conditions clean
    /// by only passing two parameters like the rules.
    bool isBase(const DIType *a, const DIType *b) const;

    /// Returns true if o is a clone.
    bool isClone(NodeID o) const;

    /// Sets the type (in objToType) of o.
    void setType(NodeID o, const DIType *t);
    /// Returns the type (from objToType) of o. Asserts existence.
    const DIType *getType(NodeID o) const;

    /// Sets the allocation site (in objToAllocation) of o.
    void setAllocationSite(NodeID o, NodeID site);
    /// Returns the allocation site (from objToAllocation) of o. Asserts existence.
    NodeID getAllocationSite(NodeID o) const;

    /// Returns objects that have clones (any key in objToClones).
    const NodeBS getObjsWithClones(void);
    /// Add a clone c to object o.
    void addClone(NodeID o, NodeID c);
    /// Returns all the clones of o.
    const NodeBS &getClones(NodeID o);

    // Set o as the original object of clone c.
    void setOriginalObj(NodeID c, NodeID o);
    /// Returns the original object c is cloned from. If c is not a clone, returns itself.
    NodeID getOriginalObj(NodeID c) const;

    /// Returns the filter set of a location. Not const; could create empty PointsTo.
    PointsTo &getFilterSet(NodeID loc);

    /// Associates gep with base (through objToGeps and memObjToGeps).
    void addGepToObj(NodeID gep, NodeID base, unsigned offset);
    /// Returns all gep objects at a particular offset for memory object.
    /// Not const; could create empty set.
    const NodeBS &getGepObjsFromMemObj(const MemObj *memObj, unsigned offset);
    /// Returns all gep objects under an object.
    /// Not const; could create empty set.
    const NodeBS &getGepObjs(NodeID base);

    /// Returns the GEP object node(s) of base for ls. This may include clones.
    /// If there are no GEP objects, then getGepObjNode is called on the PAG
    /// (through base's getGepObjNode) which will create one.
    const NodeBS getGepObjClones(NodeID base, const LocationSet& ls);

    /// Initialise the pointees of p at loc (which is type tildet *). reuse indicates
    /// whether we allow reuse. Returns whether p changed.
    bool init(NodeID loc, NodeID p, const DIType *tildet, bool reuse, bool gep=false);

    /// Returns a clone of o with type type.
    NodeID cloneObject(NodeID o, const DIType *type);

    /// Returns the ctir type attached to the value, nullptr if non-existant.
    /// Not static because it needs the DCHG to return the canonical type.
    /// Not static because we need dchg's getCanonicalType.
    const DIType *getTypeFromCTirMetadata(const Value *);

private:
    /// PTA extending this class.
    PointerAnalysis *pta;
    /// PAG the PTA uses. Just a shortcut for getPAG().
    PAG *ppag = nullptr;

    /// Object -> its type.
    std::map<NodeID, const DIType *> objToType;
    /// Object -> allocation site.
    /// The value NodeID depends on the pointer analysis (could be
    /// an SVFG node or PAG node for example).
    std::map<NodeID, NodeID> objToAllocation;
    /// (Original) object -> set of its clones.
    std::map<NodeID, NodeBS> objToClones;
    /// (Clone) object -> original object (opposite of objToclones).
    std::map<NodeID, NodeID> cloneToOriginalObj;
    /// Maps nodes (a location like a PAG node or SVFG node) to their filter set.
    std::map<NodeID, PointsTo> locToFilterSet;
    /// Maps objects to the GEP nodes beneath them.
    std::map<NodeID, NodeBS> objToGeps;
    /// Maps memory objects to their GEP objects. (memobj -> (fieldidx -> geps))
    std::map<const MemObj *, std::map<unsigned, NodeBS>> memObjToGeps;
};
