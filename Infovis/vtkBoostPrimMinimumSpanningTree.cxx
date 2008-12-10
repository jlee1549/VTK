/*=========================================================================
  
Program:   Visualization Toolkit
Module:    vtkBoostPrimMinimumSpanningTree.cxx

Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
All rights reserved.
See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*-------------------------------------------------------------------------
  Copyright 2008 Sandia Corporation.
  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
  the U.S. Government retains certain rights in this software.
  -------------------------------------------------------------------------*/
#include "vtkBoostPrimMinimumSpanningTree.h"

#include "vtkBoostGraphAdapter.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkMath.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkFloatArray.h"
#include "vtkDataArray.h"
#include "vtkSelection.h"
#include "vtkSmartPointer.h"
#include "vtkStringArray.h"
#include "vtkMutableDirectedGraph.h"
#include "vtkUndirectedGraph.h"
#include "vtkTree.h"

#include <boost/graph/prim_minimum_spanning_tree.hpp>
#include <boost/vector_property_map.hpp>
#include <boost/pending/queue.hpp>

using namespace boost;

vtkCxxRevisionMacro(vtkBoostPrimMinimumSpanningTree, "1.4");
vtkStandardNewMacro(vtkBoostPrimMinimumSpanningTree);

// Constructor/Destructor
vtkBoostPrimMinimumSpanningTree::vtkBoostPrimMinimumSpanningTree()
{
  this->EdgeWeightArrayName = 0;
  this->OriginVertexIndex = 0;
  this->ArrayName = 0;
  this->SetArrayName("Not Set");
  this->ArrayNameSet = false;
  this->NegateEdgeWeights = false;
  this->EdgeWeightMultiplier = 1;
  this->OriginValue = 0;
  this->CreateGraphVertexIdArray = false;
}

//----------------------------------------------------------------------------
vtkBoostPrimMinimumSpanningTree::~vtkBoostPrimMinimumSpanningTree()
{
  this->SetEdgeWeightArrayName(0);
  this->SetArrayName(0);
}

// Description:
// Set the index (into the vertex array) of the 
// breadth first search 'origin' vertex.
void vtkBoostPrimMinimumSpanningTree::SetOriginVertex(vtkIdType index)
{
  this->OriginVertexIndex = index;
  this->Modified();
}

// Description:
// Set the breadth first search 'origin' vertex.
// This method is basically the same as above
// but allows the application to simply specify 
// an array name and value, instead of having to
// know the specific index of the vertex.
void vtkBoostPrimMinimumSpanningTree::SetOriginVertex(
  vtkStdString arrayName, vtkVariant value)
{
  this->SetArrayName(arrayName);
  this->ArrayNameSet = true;
  this->OriginValue = value;
  this->Modified();
}

void vtkBoostPrimMinimumSpanningTree::SetNegateEdgeWeights(bool value)
{
  this->NegateEdgeWeights = value;
  if (this->NegateEdgeWeights)
    this->EdgeWeightMultiplier = -1.0;
  else
    this->EdgeWeightMultiplier = 1.0;
  
  this->Modified();
}

//----------------------------------------------------------------------------
vtkIdType vtkBoostPrimMinimumSpanningTree::GetVertexIndex(
  vtkAbstractArray *abstract, vtkVariant value)
{
  // Okay now what type of array is it
  if (abstract->IsNumeric())
    {
    vtkDataArray *dataArray = vtkDataArray::SafeDownCast(abstract);
    int intValue = value.ToInt();
    for(int i=0; i<dataArray->GetNumberOfTuples(); ++i)
      {
      if (intValue == static_cast<int>(dataArray->GetTuple1(i)))
        {
        return i;
        }
      }
    }
  else
    {
    vtkStringArray *stringArray = vtkStringArray::SafeDownCast(abstract);
    vtkStdString stringValue(value.ToString());
    for(int i=0; i<stringArray->GetNumberOfTuples(); ++i)
      {
      if (stringValue == stringArray->GetValue(i))
        {
        return i;
        }
      }
    } 
  
  // Failed
  vtkErrorMacro("Did not find a valid vertex index...");
  return 0;
}

//----------------------------------------------------------------------------
int vtkBoostPrimMinimumSpanningTree::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector)
{
  vtkErrorMacro("The Prim minimal spanning tree filter is still under development...  Use at your own risk.");
  
  // get the info objects
  vtkInformation *inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation *outInfo = outputVector->GetInformationObject(0);
  
  // get the input and output
  vtkGraph *input = vtkGraph::SafeDownCast(
    inInfo->Get(vtkDataObject::DATA_OBJECT()));
  
  // Now figure out the origin vertex of the MST
  if (this->ArrayNameSet)
    {
    vtkAbstractArray* abstract = input->GetVertexData()->GetAbstractArray(this->ArrayName);
    
    // Does the array exist at all?  
    if (abstract == NULL)
      {
      vtkErrorMacro("Could not find array named " << this->ArrayName);
      return 0;
      }
    
    this->OriginVertexIndex = this->GetVertexIndex(abstract,this->OriginValue);
    }
  
  // Retrieve the edge-weight array.
  if (!this->EdgeWeightArrayName)
    {
    vtkErrorMacro("Edge-weight array name is required");
    return 0;
    }
  vtkDataArray* edgeWeightArray = input->GetEdgeData()->GetArray(this->EdgeWeightArrayName);
  
  // Does the edge-weight array exist at all?  
  if (edgeWeightArray == NULL)
    {
    vtkErrorMacro("Could not find edge-weight array named " 
                  << this->EdgeWeightArrayName);
    return 0;
    }
  
  // Create the mutable graph to build the tree
  vtkSmartPointer<vtkMutableDirectedGraph> temp = 
    vtkSmartPointer<vtkMutableDirectedGraph>::New();
  
  // Initialize copying data into tree
  temp->GetFieldData()->PassData(input->GetFieldData());
  temp->GetVertexData()->CopyAllocate(input->GetVertexData());
  temp->GetEdgeData()->CopyAllocate(input->GetEdgeData());
  
  // Send the property map through both the multiplier and the
  // helper (for edge_descriptor indexing)
  typedef vtkGraphPropertyMapMultiplier<vtkDataArray*> mapMulti;
  mapMulti multi(edgeWeightArray,this->EdgeWeightMultiplier);
  vtkGraphEdgePropertyMapHelper<mapMulti> weight_helper(multi);
  
  // Create a predecessorMap
  vtkIdTypeArray* predecessorMap = vtkIdTypeArray::New();
  
  // Run the algorithm
  if (vtkDirectedGraph::SafeDownCast(input))
    {
    vtkDirectedGraph *g = vtkDirectedGraph::SafeDownCast(input);
    prim_minimum_spanning_tree(g, predecessorMap, weight_map(weight_helper).root_vertex(this->OriginVertexIndex) );
    }
  else
    {
    vtkUndirectedGraph *g = vtkUndirectedGraph::SafeDownCast(input);
    prim_minimum_spanning_tree(g, predecessorMap, weight_map(weight_helper).root_vertex(this->OriginVertexIndex) );
    }
  
//FIXME
  //Need to construct the tree from the predecessorMap... 
  
//FIXME
  // If the user wants it, store the predecessor mapping back to graph vertices
  if (this->CreateGraphVertexIdArray)
    {
    predecessorMap->SetName("predecessorMap");
    temp->GetVertexData()->AddArray(predecessorMap);
    }
  
  // Copy the builder graph structure into the output tree
  vtkTree *output = vtkTree::SafeDownCast(
    outInfo->Get(vtkDataObject::DATA_OBJECT()));
  if (!output->CheckedShallowCopy(temp))
    {
    vtkErrorMacro(<<"Invalid tree.");
    return 0;
    }
  
  predecessorMap->Delete();
  
  return 1;
}

//----------------------------------------------------------------------------
int vtkBoostPrimMinimumSpanningTree::FillInputPortInformation(
  int port, vtkInformation* info)
{
  // now add our info
  if (port == 0)
    {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkGraph");
    }
  return 1;
}

//----------------------------------------------------------------------------
void vtkBoostPrimMinimumSpanningTree::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "OriginVertexIndex: " << this->OriginVertexIndex << endl;
  
  os << indent << "ArrayName: " 
     << (this->ArrayName ? this->ArrayName : "(none)") << endl;
  
  os << indent << "OriginValue: " << this->OriginValue.ToString() << endl;
  
  os << indent << "ArrayNameSet: " 
     << (this->ArrayNameSet ? "true" : "false") << endl; 
  
  os << indent << "NegateEdgeWeights: " 
     << (this->NegateEdgeWeights ? "true" : "false") << endl;   
  
  os << indent << "EdgeWeightMultiplier: " << this->EdgeWeightMultiplier << endl;
  
  os << indent << "CreateGraphVertexIdArray: " 
     << (this->CreateGraphVertexIdArray ? "on" : "off") << endl;   
  
  os << indent << "EdgeWeightArrayName: " 
     << (this->EdgeWeightArrayName ? this->EdgeWeightArrayName : "(none)") 
     << endl;
}

