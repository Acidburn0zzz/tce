/**
 * @file OperationDAG.hh
 *
 * Declaration of operation directed acyclic graph class
 *
 * @author Mikael Lepistö 2007 (mikael.lepisto@tut.fi)
 * @note rating: red
 */

#ifndef TTA_OPERATION_DAG_HH
#define TTA_OPERATION_DAG_HH

#include "BoostGraph.hh"

#include "OperationDAGNode.hh"
#include "OperationDAGEdge.hh"

class TerminalNode;

class OperationDAG : 
    public BoostGraph<OperationDAGNode,OperationDAGEdge> {

public:   
    static OperationDAG null;
    bool isNull() const { return this == &null; }

    OperationDAG(const std::string& name="");
    OperationDAG(const OperationDAG& other);
    virtual ~OperationDAG();    
    bool isTrivial() const;

    int stepsToRoot(const OperationDAGNode& node) const;
    const std::set<OperationDAGNode*>& endNodes() const;

private:    
    /// Map of known step counts, if dag is changed this must be cleared.
    mutable std::map<const OperationDAGNode*, int> stepMap_;    
    /// Set of root nodes of DAG, must be cleared if dag is changed.
    mutable std::set<OperationDAGNode*> endNodes_;
        
};

#endif
