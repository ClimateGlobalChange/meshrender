///////////////////////////////////////////////////////////////////////////////
///
///	\file    GridElements.cpp
///	\author  Paul Ullrich
///	\version May 12, 2023
///
///	<remarks>
///		Copyright 2023 Paul Ullrich
///
///		This file is distributed as part of the Tempest source code package.
///		Permission is granted to use, copy, modify and distribute this
///		source code and its documentation under the terms of the GNU General
///		Public License.  This software is provided "as is" without express
///		or implied warranty.
///	</remarks>

#include "Defines.h"
#include "GridElements.h"

#include "DataArray2D.h"
#include "Announce.h"
#include "GaussQuadrature.h"
#include "STLStringHelper.h"

#include <ctime>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>
#include "netcdfcpp.h"
#include "kdtree.h"

///////////////////////////////////////////////////////////////////////////////
/// NodeTree
///////////////////////////////////////////////////////////////////////////////

NodeTree::NodeTree(
	double minimum_spacing
) :
	m_minimum_spacing(minimum_spacing),
	m_size(0)
{
	m_kdtree = kd_create(3);
	if (m_kdtree == NULL) {
		_EXCEPTIONT("kd_create(3) failed");
	}
}

///////////////////////////////////////////////////////////////////////////////

NodeTree::~NodeTree() {
	kd_free(m_kdtree);
}

///////////////////////////////////////////////////////////////////////////////

size_t NodeTree::find(
	const Node & node
) {
	kdres * kdresNearestRange = kd_nearest_range3(m_kdtree, node.x, node.y, node.z, m_minimum_spacing);
	if (kdresNearestRange == NULL) {
		_EXCEPTIONT("kd_nearest_range3() failed");
	}
	int nResSize = kd_res_size(kdresNearestRange);
	if (nResSize == 0) {
		kd_res_free(kdresNearestRange);
		return (size_t)(InvalidNode);
	}
	size_t iMinimalIndex = std::numeric_limits<int>::max();
	for (;;) {
		size_t j = (size_t)(kd_res_item_data(kdresNearestRange));
		if (j < iMinimalIndex) {
			iMinimalIndex = j;
		}

		if (kd_res_next(kdresNearestRange) == 0) {
			break;
		}
	}
	_ASSERT(iMinimalIndex != std::numeric_limits<int>::max());
	kd_res_free(kdresNearestRange);
	return iMinimalIndex;
}

///////////////////////////////////////////////////////////////////////////////

size_t NodeTree::find_or_insert(
	const Node & node,
	size_t index
) {
	int findindex = find(node);
	if (findindex != (size_t)(InvalidNode)) {
		return findindex;
	}

	kd_insert3(m_kdtree, node.x, node.y, node.z, (void*)(index));
	m_size++;
	return index;
}

///////////////////////////////////////////////////////////////////////////////
/// Face
///////////////////////////////////////////////////////////////////////////////

int Face::GetEdgeIndex(
	const Edge & edge
) const {
	for (int i = 0; i < edges.size(); i++) {
		if (edges[i] == edge) {
			return i;
		}
	}
	_EXCEPTIONT("Edge not found on Face");
}

///////////////////////////////////////////////////////////////////////////////

void Face::RemoveZeroEdges() {

	// Loop through all edges of this face
	for (int i = 0; i < edges.size(); i++) {

		// Remove zero edges
		if (edges[i][0] == edges[i][1]) {
			edges.erase(edges.begin()+i);
			i--;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

bool Face::Contains(
	const Node & n0,
	const NodeVector & nodevec
) const {
	int nParity = 0;

	for (size_t i1 = 0; i1 < edges.size(); i1++) {
		size_t i2 = (i1 + 1) % edges.size();
		
		const Node & n1 = nodevec[(*this)[i1]];
		const Node & n2 = nodevec[(*this)[i2]];

		// If both nodes are on the same size of n0.z then there will be no
		// intersection with the plane z=n0.z. If nodes are on opposite sides
		// of this plane then they must have an intersection.
		if ((n1.z > n0.z) && (n2.z > n0.z)) {
			continue;
		}
		if ((n1.z < n0.z) && (n2.z < n0.z)) {
			continue;
		}

		// Arcs of constant z aren't informative for determining inside/outside
		if (n1.z == n2.z) {
			continue;
		}

		// Intersection between n1-n2 and n0.z plane
		// Branch here to ensure result is the same regardless of n1-n2 ordering
		// Under the rules of floating point arithmetic, dA should always be
		// in the range [0,1].
		Node nx;
		if (n1.z < n2.z) {
			double dA = (n0.z - n1.z) / (n2.z - n1.z);
			nx.x = (1.0 - dA) * n1.x + dA * n2.x;
			nx.y = (1.0 - dA) * n1.y + dA * n2.y;
			nx.z = n0.z;
		} else {
			double dA = (n0.z - n2.z) / (n1.z - n2.z);
			nx.x = (1.0 - dA) * n2.x + dA * n1.x;
			nx.y = (1.0 - dA) * n2.y + dA * n1.y;
			nx.z = n0.z;
		}

		// Signed angle argument to ensure we only move towards positive longitudes
		double dc = n0.x * nx.y - n0.y * nx.x;
		double dd = n0.x * nx.x + n0.y * nx.y + n0.z * nx.z;

		// The actual angle is arctan(da), but since arctan is monotone the
		// actual angle is not needed.
		double da = dc / dd;

		if (da < 0.0) {
			continue;
		}

		// Arcs that go from smaller z to larger z have positive parity.
		// Arcs that go from larger z to smaller z have negative parity.
		if (n1.z < n2.z) {
			nParity++;

		} else {
			nParity--;
		}
	}

	if (nParity > 0) {
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////
/// Mesh
///////////////////////////////////////////////////////////////////////////////

void Mesh::Clear() {
	nodes.clear();
	faces.clear();
	edgemap.clear();
	revnodearray.clear();
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::ConstructEdgeMap() {

	// Construct the edge map
	edgemap.clear();
	for (int i = 0; i < faces.size(); i++) {
		const Face & face = faces[i];

		int nEdges = face.edges.size();

		for (int k = 0; k < nEdges; k++) {
			if (faces[i][k] == face[(k+1)%nEdges]) {
				continue;
			}

			Edge edge(face[k], face[(k+1)%nEdges]);
			FacePair facepair;

			EdgeMapIterator iter =
				edgemap.insert(EdgeMapPair(edge, facepair)).first;

			iter->second.AddFace(i);
		}
	}

	Announce("Mesh size: Edges [%i]", edgemap.size());
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::ConstructReverseNodeArray() {

	// Initialize the object
	revnodearray.resize(nodes.size());
	for (int i = 0; i < revnodearray.size(); i++) {
		revnodearray[i].clear();
	}

	// Build set for each node
	for (int i = 0; i < faces.size(); i++) {
		for (int k = 0; k < faces[i].edges.size(); k++) {
			int ixNode = faces[i].edges[k][0];
			revnodearray[ixNode].insert(i);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

Real Mesh::CalculateFaceAreas(
	bool fContainsConcaveFaces
) {
	int nCount = 0;
	vecFaceArea.Allocate(faces.size());

	if (faces.size() == 0) {
		return 0.0;
	}

	// Calculate the area of each Face
	for (int i = 0; i < faces.size(); i++) {
		vecFaceArea[i] = CalculateFaceArea(faces[i], nodes);
		if (vecFaceArea[i] < 1.0e-13) {
			nCount++;
		}
	}

	if (nCount != 0) {
		Announce("WARNING: %i small elements found", nCount);
	}

	// Calculate accumulated area carefully
	static const int Jump = 10;
	std::vector<double> vecFaceAreaBak;
	vecFaceAreaBak.resize(vecFaceArea.GetRows());
	memcpy(&(vecFaceAreaBak[0]), &(vecFaceArea[0]),
		vecFaceArea.GetRows() * sizeof(double));

	for (;;) {
		if (vecFaceAreaBak.size() == 1) {
			break;
		}
		for (int i = 0; i <= (vecFaceAreaBak.size()-1) / Jump; i++) {
			int ixRef = Jump * i;
			vecFaceAreaBak[i] = vecFaceAreaBak[ixRef];
			for (int j = 1; j < Jump; j++) {
				if (ixRef + j >= vecFaceAreaBak.size()) {
					break;
				}
				vecFaceAreaBak[i] += vecFaceAreaBak[ixRef + j];
			}
		}
		vecFaceAreaBak.resize((vecFaceAreaBak.size()-1) / Jump + 1);
	}

	return vecFaceAreaBak[0];
}

///////////////////////////////////////////////////////////////////////////////

Real Mesh::CalculateFaceAreasFromOverlap(
	const Mesh & meshOverlap
) {
	if (meshOverlap.vecFaceArea.GetRows() == 0) {
		_EXCEPTIONT("MeshOverlap Face Areas have not been calculated");
	}

	// Set all Face areas to zero
	vecFaceArea.Allocate(faces.size());

	// Loop over all Faces in meshOverlap
	double dTotalArea = 0.0;

	for (int i = 0; i < meshOverlap.faces.size(); i++) {
		int ixFirstFace = meshOverlap.vecSourceFaceIx[i];

		if (ixFirstFace >= vecFaceArea.GetRows()) {
			_EXCEPTIONT("Overlap Mesh FirstFaceIx contains invalid "
				"Face index");
		}

		vecFaceArea[ixFirstFace] += meshOverlap.vecFaceArea[i];
		dTotalArea += meshOverlap.vecFaceArea[i];
	}
	return dTotalArea;
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::ExchangeFirstAndSecondMesh() {

	// Verify all vectors are the same size
	if ((faces.size() != vecSourceFaceIx.size()) ||
		(faces.size() != vecTargetFaceIx.size())
	) {
		_EXCEPTIONT("");
	}

	// Reorder vectors
	FaceVector facesOld = faces;

	std::vector<int> vecSourceFaceIxOld = vecSourceFaceIx;

	// Reordering map
	std::multimap<int,int> multimapReorder;
	for (int i = 0; i < vecTargetFaceIx.size(); i++) {
		multimapReorder.insert(std::pair<int,int>(vecTargetFaceIx[i], i));
	}

	// Apply reordering
	faces.clear();
	vecSourceFaceIx.clear();
	vecTargetFaceIx.clear();

	std::multimap<int,int>::const_iterator iterReorder
		= multimapReorder.begin();

	for (; iterReorder != multimapReorder.end(); iterReorder++) {
		faces.push_back(facesOld[iterReorder->second]);
		vecSourceFaceIx.push_back(iterReorder->first);
		vecTargetFaceIx.push_back(vecSourceFaceIxOld[iterReorder->second]);
	}
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::RemoveCoincidentNodes() {

	// Use kdtree to find shortest distance to other nodes
	kdtree * kdt = kd_create(3);
	if (kdt == nullptr) {
		_EXCEPTIONT("Error calling kd_create(3)");
	}

	std::vector<NodeIndex> vecNewNodeIndex;
	std::vector<NodeIndex> vecUniques;

	vecNewNodeIndex.reserve(nodes.size());
	vecUniques.reserve(nodes.size());

	kd_insert3(kdt, nodes[0].x, nodes[0].y, nodes[0].z, (void*)(0));
	vecNewNodeIndex.push_back(0);
	vecUniques.push_back(0);

	for (size_t k = 1; k < nodes.size(); k++) {
		const Node & node = nodes[k];

		kdres * kdresNearest = kd_nearest3(kdt, node.x, node.y, node.z);
		if (kdresNearest == NULL) {
			_EXCEPTIONT("kd_nearest3() failed");
		}
		Node nodeNearest;
		size_t ixNodeNearestNewIx =
			(size_t)(kd_res_item3(kdresNearest, &(nodeNearest.x), &(nodeNearest.y), &(nodeNearest.z)));
		kd_res_free(kdresNearest);

		Node nodeDelta = node - nodeNearest;
		if (nodeDelta.Magnitude() < ReferenceTolerance) {
			vecNewNodeIndex.push_back((NodeIndex)ixNodeNearestNewIx);
		} else {
			kd_insert3(kdt, node.x, node.y, node.z, (void*)(vecUniques.size()));
			vecNewNodeIndex.push_back((NodeIndex)(vecUniques.size()));
			vecUniques.push_back((NodeIndex)k);
		}
	}

	kd_free(kdt);

	// Number of uniques 
	if (vecUniques.size() == nodes.size()) {
		return;
	}

	Announce("%i duplicate nodes detected", nodes.size() - vecUniques.size());

	// Remove duplicates from nodes vector
	{
		NodeVector nodesOld = nodes;
		nodes.resize(vecUniques.size());
		for (size_t i = 0; i < vecUniques.size(); i++) {
			nodes[i] = nodesOld[vecUniques[i]];
		}
	}

	// Adjust node indices in Faces
	for (Face & face : faces) {
		for (Edge & edge : face.edges) {
			edge[0] = vecNewNodeIndex[edge[0]];
			edge[1] = vecNewNodeIndex[edge[1]];
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::Write(
	const std::string & strFile,
	NcFile::FileFormat eFileFormat
) const {
	const int ParamFour = 4;
	const int ParamLenString = 33;

	// Temporarily change error reporting
	NcError error_temp(NcError::verbose_fatal);

	// Determine block sizes
	std::vector<int> vecBlockSizes;
	std::vector<int> vecBlockSizeFaces;
	{
		std::map<int, int> mapBlockSizes;
		std::map<int, int>::iterator iterBlockSize;
		int iBlock;
		char szBuffer[ParamLenString];

		for (int i = 0; i < faces.size(); i++) {
			iterBlockSize = mapBlockSizes.find(faces[i].edges.size());

			if (iterBlockSize == mapBlockSizes.end()) {
				mapBlockSizes.insert(
					std::pair<int,int>(faces[i].edges.size(), 1));
			} else {
				(iterBlockSize->second)++;
			}
		}

		vecBlockSizes.resize(mapBlockSizes.size());
		vecBlockSizeFaces.resize(mapBlockSizes.size());

		AnnounceStartBlock("Nodes per element");
		iterBlockSize = mapBlockSizes.begin();
		iBlock = 1;
		for (; iterBlockSize != mapBlockSizes.end(); iterBlockSize++) {
			vecBlockSizes[iBlock-1] = iterBlockSize->first;
			vecBlockSizeFaces[iBlock-1] = iterBlockSize->second;

			Announce("Block %i (%i nodes): %i",
				iBlock, vecBlockSizes[iBlock-1], vecBlockSizeFaces[iBlock-1]);

			iBlock++;
		}
		AnnounceEndBlock(NULL);
	}

	// Output to a NetCDF Exodus file
	NcFile ncOut(strFile.c_str(), NcFile::Replace, NULL, 0, eFileFormat);
	if (!ncOut.is_valid()) {
		_EXCEPTION1("Unable to open grid file \"%s\" for writing",
			strFile.c_str());
	}

	// Auxiliary Exodus dimensions
	NcDim * dimLenString = ncOut.add_dim("len_string", ParamLenString);
	NcDim * dimLenLine = ncOut.add_dim("len_line", 81);
	NcDim * dimFour = ncOut.add_dim("four", ParamFour);
	NcDim * dimTime = ncOut.add_dim("time_step");
	NcDim * dimDimension = ncOut.add_dim("num_dim", 3);

	// Number of nodes
	int nNodeCount = nodes.size();
	NcDim * dimNodes = ncOut.add_dim("num_nodes", nNodeCount);

	// Number of elements
	int nElementCount = faces.size();
	NcDim * dimElements = ncOut.add_dim("num_elem", nElementCount);

	// Other dimensions
	NcDim * dimNumQARec = ncOut.add_dim("num_qa_rec", 1);

	// Global attributes
	ncOut.add_att("api_version", 5.00f);
	ncOut.add_att("version", 5.00f);
	ncOut.add_att("floating_point_word_size", 8);
	ncOut.add_att("file_size", 0);

	// Current time
	{
		time_t t = time(0);
		struct tm * timestruct = localtime(&t);

		char szDate[ParamLenString];
		char szTime[ParamLenString];

		strftime(szDate, sizeof(szDate), "%m/%d/%Y", timestruct);
		strftime(szTime, sizeof(szTime), "%X", timestruct);

		char szTitle[128];
		snprintf(szTitle, 128, "tempest(%s) %s: %s", strFile.c_str(), szDate, szTime);
		ncOut.add_att("title", szTitle);

		// Time_whole (unused)
		NcVar * varTimeWhole = ncOut.add_var("time_whole", ncDouble, dimTime);
		if (varTimeWhole == NULL) {
			_EXCEPTIONT("Error creating variable \"time_whole\"");
		}

		// QA records
		char szQARecord[ParamFour][ParamLenString]
			= {"Tempest", "14.0", "", ""};

		strcpy(szQARecord[2], szDate);
		strcpy(szQARecord[3], szTime);

		NcVar * varQARecords =
			ncOut.add_var("qa_records", ncChar,
				dimNumQARec, dimFour, dimLenString);

		if (varQARecords == NULL) {
			_EXCEPTIONT("Error creating variable \"qa_records\"");
		}

		varQARecords->set_cur(0, 0, 0);
		varQARecords->put(&(szQARecord[0][0]), 1, 4, ParamLenString);
	}

	// Coordinate names
	{
		char szCoordNames[3][ParamLenString] = {"x", "y", "z"};

		NcVar * varCoordNames =
			ncOut.add_var("coor_names", ncChar, dimDimension, dimLenString);

		if (varCoordNames == NULL) {
			_EXCEPTIONT("Error creating variable \"coor_names\"");
		}

		varCoordNames->set_cur(0, 0, 0);
		varCoordNames->put(&(szCoordNames[0][0]), 3, ParamLenString);
	}

	// Element blocks
	NcDim * dimNumElementBlocks =
		ncOut.add_dim("num_el_blk", vecBlockSizes.size());

	if (dimNumElementBlocks == NULL) {
		_EXCEPTIONT("Error creating dimension \"num_el_blk\"");
	}

	std::vector<NcDim *> vecElementBlockDim;
	std::vector<NcDim *> vecNodesPerElementDim;
	std::vector<NcDim *> vecAttBlockDim;

	vecElementBlockDim.resize(vecBlockSizes.size());
	vecNodesPerElementDim.resize(vecBlockSizes.size());
	vecAttBlockDim.resize(vecBlockSizes.size());

	char szBuffer[ParamLenString];
	for (int n = 0; n < vecBlockSizes.size(); n++) {
		snprintf(szBuffer, ParamLenString, "num_el_in_blk%i", n+1);
		vecElementBlockDim[n] =
			ncOut.add_dim(szBuffer, vecBlockSizeFaces[n]);

		if (vecElementBlockDim[n] == NULL) {
			_EXCEPTION1("Error creating dimension \"%s\"", szBuffer);
		}

		snprintf(szBuffer, ParamLenString, "num_nod_per_el%i", n+1);
		vecNodesPerElementDim[n] =
			ncOut.add_dim(szBuffer, vecBlockSizes[n]);

		if (vecNodesPerElementDim[n] == NULL) {
			_EXCEPTION1("Error creating dimension \"%s\"", szBuffer);
		}

		snprintf(szBuffer, ParamLenString, "num_att_in_blk%i", n+1);
		vecAttBlockDim[n] =
			ncOut.add_dim(szBuffer, 1);

		if (vecAttBlockDim[n] == NULL) {
			_EXCEPTION1("Error creating dimension \"%s\"", szBuffer);
		}
	}

	// Element block names
	{
		NcVar * varElementBlockNames =
			ncOut.add_var("eb_names", ncChar,
				dimNumElementBlocks, dimLenString);

		if (varElementBlockNames == NULL) {
			_EXCEPTIONT("Error creating dimension \"eb_names\"");
		}
	}

	// Element block status and property
	{
		std::vector<int> vecStatus;
		std::vector<int> vecProp;
		vecStatus.resize(vecBlockSizes.size());
		vecProp.resize(vecBlockSizes.size());

		for (int n = 0; n < vecBlockSizes.size(); n++) {
			vecStatus[n] = 1;
			vecProp[n] = n+1;
		}

		NcVar * varElementBlockStatus =
			ncOut.add_var("eb_status", ncInt, dimNumElementBlocks);

		if (varElementBlockStatus == NULL) {
			_EXCEPTIONT("Error creating variable \"eb_status\"");
		}

		varElementBlockStatus->put(&(vecStatus[0]), vecBlockSizes.size());

		NcVar * varElementProperty =
			ncOut.add_var("eb_prop1", ncInt, dimNumElementBlocks);

		if (varElementProperty == NULL) {
			_EXCEPTIONT("Error creating variable \"eb_prop1\"");
		}

		varElementProperty->put(&(vecProp[0]), vecBlockSizes.size());
		varElementProperty->add_att("name", "ID");
	}

	// Attributes
	{
		for (int n = 0; n < vecBlockSizes.size(); n++) {
			std::vector<double> dAttrib;
			dAttrib.resize(vecBlockSizeFaces[n]);
			for (int i = 0; i < vecBlockSizeFaces[n]; i++) {
				dAttrib[i] = 1.0;
			}

			char szAttribName[ParamLenString];
			snprintf(szAttribName, ParamLenString, "attrib%i", n+1);

			NcVar * varAttrib =
				ncOut.add_var(
					szAttribName, ncDouble,
					vecElementBlockDim[n],
					vecAttBlockDim[n]);

			if (varAttrib == NULL) {
				_EXCEPTION1("Error creating variable \"%s\"", szAttribName);
			}

			varAttrib->set_cur((long)0);
			varAttrib->put(&(dAttrib[0]), vecBlockSizeFaces[n]);
		}
	}

	// Face-specific variables
	{
		// Face nodes (1-indexed)
		std::vector<NcVar*> vecConnectVar;
		vecConnectVar.resize(vecBlockSizes.size());

		std::vector< DataArray2D<int> > vecConnect;
		vecConnect.resize(vecBlockSizes.size());

		// Number of elements added to each connectivity array
		std::vector<int> vecConnectCount;
		vecConnectCount.resize(vecBlockSizes.size());

		// Global ids
		std::vector<NcVar*> vecGlobalIdVar;
		vecGlobalIdVar.resize(vecBlockSizes.size());

		std::vector< DataArray1D<int> > vecGlobalId;
		vecGlobalId.resize(vecBlockSizes.size());

		// Edge types
		std::vector<NcVar*> vecEdgeTypeVar;
		vecEdgeTypeVar.resize(vecBlockSizes.size());

		std::vector< DataArray2D<int> > vecEdgeType;
		vecEdgeType.resize(vecBlockSizes.size());

		// Parent on source mesh
		std::vector<NcVar*> vecFaceParentAVar;
		vecFaceParentAVar.resize(vecBlockSizes.size());

		std::vector< DataArray1D<int> > vecFaceParentA;
		vecFaceParentA.resize(vecBlockSizes.size());

		// Parent on target mesh
		std::vector<NcVar*> vecFaceParentBVar;
		vecFaceParentBVar.resize(vecBlockSizes.size());

		std::vector< DataArray1D<int> > vecFaceParentB;
		vecFaceParentB.resize(vecBlockSizes.size());

		// Initialize block-local storage arrays and create output variables
		for (int n = 0; n < vecBlockSizes.size(); n++) {
			vecConnect[n].Allocate(vecBlockSizeFaces[n], vecBlockSizes[n]);
			vecGlobalId[n].Allocate(vecBlockSizeFaces[n]);
			vecEdgeType[n].Allocate(vecBlockSizeFaces[n], vecBlockSizes[n]);
	
			char szConnectVarName[ParamLenString];
			snprintf(szConnectVarName, ParamLenString, "connect%i", n+1);
			vecConnectVar[n] =
				ncOut.add_var(
					szConnectVarName, ncInt,
					vecElementBlockDim[n],
					vecNodesPerElementDim[n]);

			if (vecConnectVar[n] == NULL) {
				_EXCEPTION1("Error creating variable \"%s\"",
					szConnectVarName);
			}

			char szConnectAttrib[ParamLenString];
			snprintf(szConnectAttrib, ParamLenString, "SHELL%i", vecBlockSizes[n]);
			vecConnectVar[n]->add_att("elem_type", szConnectAttrib);

			char szGlobalIdVarName[ParamLenString];
			snprintf(szGlobalIdVarName, ParamLenString, "global_id%i", n+1);
			vecGlobalIdVar[n] = 
				ncOut.add_var(
					szGlobalIdVarName, ncInt,
					vecElementBlockDim[n]);

			if (vecGlobalIdVar[n] == NULL) {
				_EXCEPTION1("Error creating variable \"%s\"",
					szGlobalIdVarName);
			}

			char szEdgeTypeVarName[ParamLenString];
			snprintf(szEdgeTypeVarName, ParamLenString, "edge_type%i", n+1);
			vecEdgeTypeVar[n] =
				ncOut.add_var(
					szEdgeTypeVarName, ncInt,
					vecElementBlockDim[n],
					vecNodesPerElementDim[n]);

			if (vecEdgeTypeVar[n] == NULL) {
				_EXCEPTION1("Error creating variable \"%s\"",
					szEdgeTypeVarName);
			}

			if (vecSourceFaceIx.size() != 0) {
				vecFaceParentA[n].Allocate(vecBlockSizeFaces[n]);

				char szParentAVarName[ParamLenString];
				snprintf(szParentAVarName, ParamLenString, "el_parent_a%i", n+1);
				vecFaceParentAVar[n] =
					ncOut.add_var(
						szParentAVarName, ncInt,
						vecElementBlockDim[n]);

				if (vecFaceParentAVar[n] == NULL) {
					_EXCEPTION1("Error creating variable \"%s\"",
						szParentAVarName);
				}
			}

			if (vecTargetFaceIx.size() != 0) {
				vecFaceParentB[n].Allocate(vecBlockSizeFaces[n]);

				char szParentBVarName[ParamLenString];
				snprintf(szParentBVarName, ParamLenString, "el_parent_b%i", n+1);
				vecFaceParentBVar[n] =
					ncOut.add_var(
						szParentBVarName, ncInt,
						vecElementBlockDim[n]);

				if (vecFaceParentBVar[n] == NULL) {
					_EXCEPTION1("Error creating variable \"%s\"",
						szParentBVarName);
				}
			}
		}

		// Rebuild global data structures in local block arrays
		for (int i = 0; i < nElementCount; i++) {
			int iBlock = 0;
			for (; iBlock < vecBlockSizes.size(); iBlock++) {
				if (vecBlockSizes[iBlock] == faces[i].edges.size()) {
					break;
				}
			}
			if (iBlock == vecBlockSizes.size()) {
				_EXCEPTIONT("Logic error");
			}

			int iLocal = vecConnectCount[iBlock];
			for (int k = 0; k < faces[i].edges.size(); k++) {
				vecConnect[iBlock][iLocal][k] = faces[i][k] + 1;

				vecEdgeType[iBlock][iLocal][k] =
					static_cast<int>(faces[i].edges[k].type);
			}

			vecGlobalId[iBlock][iLocal] = i + 1;

			if (vecSourceFaceIx.size() != 0) {
				vecFaceParentA[iBlock][iLocal] = vecSourceFaceIx[i] + 1;
			}
			if (vecTargetFaceIx.size() != 0) {
				vecFaceParentB[iBlock][iLocal] = vecTargetFaceIx[i] + 1;
			}

			vecConnectCount[iBlock]++;
		}

		// Write data to NetCDF file
		for (int n = 0; n < vecBlockSizes.size(); n++) {
			vecConnectVar[n]->set_cur(0, 0);
			vecConnectVar[n]->put(
				&(vecConnect[n][0][0]),
				vecBlockSizeFaces[n],
				vecBlockSizes[n]);

			vecGlobalIdVar[n]->set_cur((long)0);
			vecGlobalIdVar[n]->put(
				&(vecGlobalId[n][0]),
				vecBlockSizeFaces[n]);

			vecEdgeTypeVar[n]->set_cur(0, 0);
			vecEdgeTypeVar[n]->put(
				&(vecEdgeType[n][0][0]),
				vecBlockSizeFaces[n],
				vecBlockSizes[n]);

			if (vecSourceFaceIx.size() != 0) {
				vecFaceParentAVar[n]->set_cur((long)0);
				vecFaceParentAVar[n]->put(
					&(vecFaceParentA[n][0]),
					vecBlockSizeFaces[n]);
			}

			if (vecTargetFaceIx.size() != 0) {
				vecFaceParentBVar[n]->set_cur((long)0);
				vecFaceParentBVar[n]->put(
					&(vecFaceParentB[n][0]),
					vecBlockSizeFaces[n]);
			}
		}
	}

	// Node list
	{
		NcVar * varNodes =
			ncOut.add_var("coord", ncDouble, dimDimension, dimNodes);

		if (varNodes == NULL) {
			_EXCEPTIONT("Error creating variable \"coord\"");
		}

		DataArray1D<double> dCoord(nNodeCount);

		for (int i = 0; i < nNodeCount; i++) {
			dCoord[i] = static_cast<double>(nodes[i].x);
		}
		varNodes->set_cur(0, 0);
		varNodes->put(dCoord, 1, nNodeCount);
		for (int i = 0; i < nNodeCount; i++) {
			dCoord[i] = static_cast<double>(nodes[i].y);
		}
		varNodes->set_cur(1, 0);
		varNodes->put(dCoord, 1, nNodeCount);
		for (int i = 0; i < nNodeCount; i++) {
			dCoord[i] = static_cast<double>(nodes[i].z);
		}
		varNodes->set_cur(2, 0);
		varNodes->put(dCoord, 1, nNodeCount);
	}
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::WriteScrip(
	const std::string & strFile,
	NcFile::FileFormat eFileFormat
) const {
	const int ParamLenString = 33;

	// Temporarily change error reporting
	NcError error_temp(NcError::verbose_fatal);

	//---------------------------------------------------------------------------
	// Determine block sizes
	std::vector<int> vecBlockSizes;
	std::vector<int> vecBlockSizeFaces;
	{
		std::map<int, int> mapBlockSizes;
		std::map<int, int>::iterator iterBlockSize;
		int iBlock;
		char szBuffer[ParamLenString];

		for (int i = 0; i < faces.size(); i++) {
			iterBlockSize = mapBlockSizes.find(faces[i].edges.size());

			if (iterBlockSize == mapBlockSizes.end()) {
				mapBlockSizes.insert(
					std::pair<int,int>(faces[i].edges.size(), 1));
			} else {
				(iterBlockSize->second)++;
			}
		}

		vecBlockSizes.resize(mapBlockSizes.size());
		vecBlockSizeFaces.resize(mapBlockSizes.size());

		AnnounceStartBlock("Nodes per element");
		iterBlockSize = mapBlockSizes.begin();
		iBlock = 1;
		for (; iterBlockSize != mapBlockSizes.end(); iterBlockSize++) {
			vecBlockSizes[iBlock-1] = iterBlockSize->first;
			vecBlockSizeFaces[iBlock-1] = iterBlockSize->second;

			Announce("Block %i (%i nodes): %i",
				iBlock, vecBlockSizes[iBlock-1], vecBlockSizeFaces[iBlock-1]);

			iBlock++;
		}
		AnnounceEndBlock(NULL);
	}
	//---------------------------------------------------------------------------
	// Output to a NetCDF SCRIP file
	NcFile ncOut(strFile.c_str(), NcFile::Replace, NULL, 0, eFileFormat);
	if (!ncOut.is_valid()) {
		_EXCEPTION1("Unable to open grid file \"%s\" for writing",
			strFile.c_str());
	}
	// Find max number of corners oer all faces
	int nElementCount = faces.size();
	int nCornersMax = 0;
	for (int i=0; i<nElementCount; i++) {
		nCornersMax = std::max( nCornersMax, (int)(faces[i].edges.size()) );
	}
	// SCRIP dimensions
	NcDim * dimGridSize   = ncOut.add_dim("grid_size",    nElementCount);
	NcDim * dimGridCorner = ncOut.add_dim("grid_corners", nCornersMax);
	NcDim * dimGridRank   = ncOut.add_dim("grid_rank",    1);
	// Global attributes
	ncOut.add_att("api_version", 5.00f);
	ncOut.add_att("version", 5.00f);
	ncOut.add_att("floating_point_word_size", 8);
	ncOut.add_att("file_size", 0);
	//---------------------------------------------------------------------------
	// Grid Area
	{
		NcVar * varArea = ncOut.add_var("grid_area", ncDouble, dimGridSize);
		if (varArea == NULL) {
			_EXCEPTIONT("Error creating variable \"grid_area\"");
		}
		DataArray1D<double> area(nElementCount);
		for (int i=0; i<nElementCount; i++) {
			area[i] = static_cast<double>( CalculateFaceArea(faces[i], nodes) );
		}
		varArea->set_cur((long)0);
		varArea->put(area, nElementCount);
		varArea->add_att("units", "radians^2");
	}
	//---------------------------------------------------------------------------
	// Grid center and corner coordinates
	{
		NcVar * varCenterLat = ncOut.add_var("grid_center_lat", ncDouble, dimGridSize);
		NcVar * varCenterLon = ncOut.add_var("grid_center_lon", ncDouble, dimGridSize);
		NcVar * varCornerLat = ncOut.add_var("grid_corner_lat", ncDouble, dimGridSize, dimGridCorner);
		NcVar * varCornerLon = ncOut.add_var("grid_corner_lon", ncDouble, dimGridSize, dimGridCorner);
		if (varCenterLat == NULL) {
			_EXCEPTIONT("Error creating variable \"grid_center_lat\"");
		}
		if (varCenterLon == NULL) {
			_EXCEPTIONT("Error creating variable \"grid_center_lon\"");
		}
		if (varCornerLat == NULL) {
			_EXCEPTIONT("Error creating variable \"grid_corner_lat\"");
		}
		if (varCornerLon == NULL) {
			_EXCEPTIONT("Error creating variable \"grid_corner_lon\"");
		}

		DataArray1D<double> centerLat(nElementCount);
		DataArray1D<double> centerLon(nElementCount);
		DataArray2D<double> cornerLat(nElementCount, nCornersMax);
		DataArray2D<double> cornerLon(nElementCount, nCornersMax);
		Node corner(0,0,0);
		for (int i=0; i<nElementCount; i++) {
			Node center(0,0,0);
			// int nCorners = faces[i].edges.size()+1;
			int nCorners = faces[i].edges.size();
			for (int j=0; j<nCorners; ++j) {
				corner = nodes[ faces[i][j] ];
				XYZtoRLL_Deg(
					corner.x, corner.y, corner.z,
					cornerLon[i][j],
					cornerLat[i][j]);
				center = center + corner;
			}
			center = center / nCorners;
			double dMag = sqrt(center.x * center.x + 
							   center.y * center.y + 
							   center.z * center.z);
			center.x /= dMag;
			center.y /= dMag;
			center.z /= dMag;
			XYZtoRLL_Deg(
				center.x, center.y, center.z,
				centerLon[i],
				centerLat[i]);
			// Adjust corner logitudes
			double lonDiff;
			for (int j=0; j<nCorners; ++j) {
				// First check for polar point
				if (cornerLat[i][j]==90. || cornerLat[i][j]==-90.) {
					cornerLon[i][j] = centerLon[i];
				}
				// Next check for corners that wrap around prime meridian
				lonDiff = centerLon[i] - cornerLon[i][j];
				if (lonDiff>180) {
					cornerLon[i][j] = cornerLon[i][j] + (double)360.0;
				}
				if (lonDiff<-180) {
					cornerLon[i][j] = cornerLon[i][j] - (double)360.0;
				}
			}
		}
		varCenterLat->set_cur((long)0);
		varCenterLat->put(centerLat, nElementCount);
		varCenterLat->add_att("units", "degrees");
		varCenterLat->add_att("_FillValue", 9.96920996838687e+36 );

		varCenterLon->set_cur((long)0);
		varCenterLon->put(centerLon, nElementCount);
		varCenterLon->add_att("units", "degrees");
		varCenterLon->add_att("_FillValue", 9.96920996838687e+36 );

		for (int i=0; i<nElementCount; i++) {
			varCornerLat->set_cur(i,0);
			varCornerLat->put(cornerLat[i], 1, nCornersMax);
			varCornerLon->set_cur(i,0);
			varCornerLon->put(cornerLon[i], 1, nCornersMax);
		}
		varCornerLat->add_att("units", "degrees");
		varCornerLon->add_att("units", "degrees");
		varCornerLat->add_att("_FillValue", 9.96920996838687e+36 );
		varCornerLon->add_att("_FillValue", 9.96920996838687e+36 );
	}
	//---------------------------------------------------------------------------
	// Grid mask
	{
		NcVar * varMask = ncOut.add_var("grid_imask", ncDouble, dimGridSize);
		if (varMask == NULL) {
			_EXCEPTIONT("Error creating variable \"grid_imask\"");
		}
		DataArray1D<double> mask(nElementCount);
		for (int i = 0; i < nElementCount; i++) {
			mask[i] = static_cast<double>( 1 );
		}
		varMask->set_cur((long)0);
		varMask->put(mask, nElementCount);
		varMask->add_att("_FillValue", 9.96920996838687e+36 );
	}
	//---------------------------------------------------------------------------
	// Grid dims
	{
		NcVar * varDims = ncOut.add_var("grid_dims", ncInt, dimGridRank);
		if (varDims == NULL) {
			_EXCEPTIONT("Error creating variable \"grid_dims\"");
		}
		DataArray1D<int> rank(1);
		rank(0) = 1;
		varDims->set_cur((long)0);
		varDims->put(rank, 1);
	}
	//---------------------------------------------------------------------------
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::Read(
	const std::string & strFile,
	bool fRemoveCoincidentNodes
) {

	const int ParamFour = 4;
	const int ParamLenString = 33;

	// Turn off fatal errors in NetCDF
	NcError error(NcError::silent_nonfatal);

	// Store the file name
	strFileName = strFile;

	// Open the NetCDF file
	if (strFile == "") {
		_EXCEPTIONT("No grid file specified for reading");
	}
	NcFile ncFile(strFile.c_str(), NcFile::ReadOnly);
	if (!ncFile.is_valid()) {
		_EXCEPTION1("Unable to open grid file \"%s\" for reading",
			strFile.c_str());
	}

	// Check for global attribute title = "ICON grid description" 
	NcAtt * attICON = ncFile.get_att("title");
	if (attICON != NULL) {
		std::string strAttTitle = attICON->as_string(0);
		if (strAttTitle == "ICON grid description") {
			NcDim * dimVertex = ncFile.get_dim("vertex");
			if (dimVertex == NULL) {
				_EXCEPTION1("ICON grid file \"%s\" missing dimension \"vertex\"",
					strFile.c_str());
			}
			NcDim * dimCell = ncFile.get_dim("cell");
			if (dimVertex == NULL) {
				_EXCEPTION1("ICON grid file \"%s\" missing dimension \"cell\"",
					strFile.c_str());
			}

			nodes.resize(dimVertex->size());

			DataArray1D<double> dNodeBuffer(dimVertex->size());

			// Load in x coordinates of vertices
			NcVar * varICONX = ncFile.get_var("cartesian_x_vertices");
			if (varICONX == NULL) {
				_EXCEPTION1("ICON grid file \"%s\" missing variable \"cartesian_x_vertices\"",
					strFile.c_str());
			}
			if (varICONX->num_dims() != 1) {
				_EXCEPTION1("ICON grid file \"%s\" variable \"cartesian_x_vertices\" must have dimension 1",
					strFile.c_str());
			}
			if (std::string(varICONX->get_dim(0)->name()) != "vertex") {
				_EXCEPTION1("ICON grid file \"%s\" variable \"cartesian_x_vertices\" dimension 0 must have name \"vertex\"",
					strFile.c_str());
			}
			varICONX->set_cur((long)0);
			varICONX->get(&(dNodeBuffer[0]), dimVertex->size());
			for (long i = 0; i < dimVertex->size(); i++) {
				nodes[i].x = dNodeBuffer[i];
			}

			// Load in y coordinates of vertices
			NcVar * varICONY = ncFile.get_var("cartesian_y_vertices");
			if (varICONY == NULL) {
				_EXCEPTION1("ICON grid file \"%s\" missing variable \"cartesian_y_vertices\"",
					strFile.c_str());
			}
			if (varICONY->num_dims() != 1) {
				_EXCEPTION1("ICON grid file \"%s\" variable \"cartesian_y_vertices\" must have dimension 1",
					strFile.c_str());
			}
			if (std::string(varICONY->get_dim(0)->name()) != "vertex") {
				_EXCEPTION1("ICON grid file \"%s\" variable \"cartesian_y_vertices\" dimension 0 must have name \"vertex\"",
					strFile.c_str());
			}
			varICONY->set_cur((long)0);
			varICONY->get(&(dNodeBuffer[0]), dimVertex->size());
			for (long i = 0; i < dimVertex->size(); i++) {
				nodes[i].y = dNodeBuffer[i];
			}

			// Load in z coordinates of vertices
			NcVar * varICONZ = ncFile.get_var("cartesian_z_vertices");
			if (varICONZ == NULL) {
				_EXCEPTION1("ICON grid file \"%s\" missing variable \"cartesian_z_vertices\"",
					strFile.c_str());
			}
			if (varICONZ->num_dims() != 1) {
				_EXCEPTION1("ICON grid file \"%s\" variable \"cartesian_z_vertices\" must have dimension 1",
					strFile.c_str());
			}
			if (std::string(varICONZ->get_dim(0)->name()) != "vertex") {
				_EXCEPTION1("ICON grid file \"%s\" variable \"cartesian_z_vertices\" dimension 0 must have name \"vertex\"",
					strFile.c_str());
			}
			varICONZ->set_cur((long)0);
			varICONZ->get(&(dNodeBuffer[0]), dimVertex->size());
			for (long i = 0; i < dimVertex->size(); i++) {
				nodes[i].z = dNodeBuffer[i];
			}

			dNodeBuffer.Detach();

			// Load in face vertex indices
			NcVar * varVertexOfCell = ncFile.get_var("vertex_of_cell");
			if (varVertexOfCell == NULL) {
				_EXCEPTION1("ICON grid file \"%s\" missing variable \"vertex_of_cell\"",
					strFile.c_str());
			}
			if (varVertexOfCell->num_dims() != 2) {
				_EXCEPTION1("ICON grid file \"%s\" variable \"vertex_of_cell\" must have dimension 2",
					strFile.c_str());
			}
			if (std::string(varVertexOfCell->get_dim(1)->name()) != "cell") {
				_EXCEPTION1("ICON grid file \"%s\" variable \"vertex_of_cell\" dimension 1 must have name \"cell\"",
					strFile.c_str());
			}

			long lVerticesPerCell = varVertexOfCell->get_dim(0)->size();

			faces.resize(dimCell->size(), Face(lVerticesPerCell));

			DataArray2D<int> dVertexOfCellBuf(
				lVerticesPerCell,
				dimCell->size());
			varVertexOfCell->get(
				&(dVertexOfCellBuf(0,0)), 
				lVerticesPerCell,
				dimCell->size());

			for (long i = 0; i < dimCell->size(); i++) {
				for (long j = 0; j < lVerticesPerCell; j++) {
					if ((dVertexOfCellBuf(j,i) < 1) || (dVertexOfCellBuf(j,i) > nodes.size())) {
						_EXCEPTION4("ICON grid file \"%s\" vertex %li cell %li out of range (%li)",
							strFile.c_str(), j, i, dVertexOfCellBuf(j,i));
					}
					faces[i].SetNode(j, dVertexOfCellBuf(j,i)-1);
				}
			}
			return;
		}
	}

	// Check for dimension names "grid_size", "grid_rank" and "grid_corners"
	int iSCRIPFormat = 0;
	for (int i = 0; i < ncFile.num_dims(); i++) {
		NcDim * dim = ncFile.get_dim(i);
		std::string strDimName = dim->name();
		if (strDimName == "grid_size") {
			iSCRIPFormat++;
		}
		if (strDimName == "grid_corners") {
			iSCRIPFormat++;
		}
		if (strDimName == "grid_rank") {
			iSCRIPFormat++;
		}
	}

	// Input from a NetCDF SCRIP file
	if (iSCRIPFormat == 3) {
		Announce("SCRIP Format File detected");

		NcDim * dimGridSize = ncFile.get_dim("grid_size");
		NcDim * dimGridCorners = ncFile.get_dim("grid_corners");

		// Check rank
		NcDim * dimGridRank = ncFile.get_dim("grid_rank");

		// Get the grid corners
		NcVar * varGridCornerLat = ncFile.get_var("grid_corner_lat");
		NcVar * varGridCornerLon = ncFile.get_var("grid_corner_lon");

		if (varGridCornerLat == NULL) {
			_EXCEPTION1("SCRIP Grid file \"%s\" is missing variable "
					"\"grid_corner_lat\"", strFile.c_str());
		}
		if (varGridCornerLon == NULL) {
			_EXCEPTION1("SCRIP Grid file \"%s\" is missing variable "
					"\"grid_corner_lon\"", strFile.c_str());
		}

		int nGridSize = static_cast<int>(dimGridSize->size());
		int nGridCorners = static_cast<int>(dimGridCorners->size());

		DataArray2D<double> dCornerLat(nGridSize, nGridCorners);
		DataArray2D<double> dCornerLon(nGridSize, nGridCorners);

		varGridCornerLat->set_cur(0, 0);
		varGridCornerLat->get(&(dCornerLat[0][0]), nGridSize, nGridCorners);

		varGridCornerLon->set_cur(0, 0);
		varGridCornerLon->get(&(dCornerLon[0][0]), nGridSize, nGridCorners);

		faces.resize(nGridSize);
		nodes.resize(nGridSize * nGridCorners);

		// Check for units attribute; if "degrees" then convert to radians
		bool fConvertLonToRadians = false;
		NcAtt * attGridCornerLonUnits = varGridCornerLon->get_att("units");

		if (attGridCornerLonUnits != NULL) {
			std::string strLonUnits = attGridCornerLonUnits->as_string(0);
			STLStringHelper::ToLower(strLonUnits);
			if (strLonUnits == "degrees") {
				fConvertLonToRadians = true;
			}
		}

		bool fConvertLatToRadians = false;
		NcAtt * attGridCornerLatUnits = varGridCornerLat->get_att("units");

		if (attGridCornerLatUnits != NULL) {
			std::string strLatUnits = attGridCornerLatUnits->as_string(0);
			STLStringHelper::ToLower(strLatUnits);
			if (strLatUnits == "degrees") {
				fConvertLatToRadians = true;
			}
		}

		// Load mask variable
		NcVar * varMask = ncFile.get_var("grid_imask");
		if (varMask != NULL) {
			if (varMask->num_dims() != 1) {
				_EXCEPTIONT("Unknown format of variable \"grid_imask\": "
					"More than one dimension");
			}
			if (varMask->get_dim(0)->size() != nGridSize) {
				_EXCEPTIONT("Unknown format of variable \"grid_imask\": "
					"Incorrect first dimension size");
			}
			//if (varMask->type() != ncInt) {
			//	_EXCEPTIONT("Unknown format of variable \"grid_imask\": "
			//		"Expected int type");
			//}

			vecMask.Allocate(nGridSize);
			varMask->get(&(vecMask[0]), nGridSize);
		}

		// Current global node index
		int ixNode = 0;

		for (int i = 0; i < nGridSize; i++) {

			// Create a new Face
			Face faceNew(nGridCorners);
			for (int j = 0; j < nGridCorners; j++) {
				faceNew.SetNode(j, ixNode + j);
			}
			faces[i] = faceNew;

			// Insert Face corners into node table
			for (int j = 0; j < nGridCorners; j++) {
				double dLon = dCornerLon[i][j];
				double dLat = dCornerLat[i][j];

				if (fConvertLonToRadians) {
					dLon = dLon / 180.0 * M_PI;
				}
				if (fConvertLatToRadians) {
					dLat = dLat / 180.0 * M_PI;
				}

				if (dLat > 0.5 * M_PI) {
					dLat = 0.5 * M_PI;
				}
				if (dLat < -0.5 * M_PI) {
					dLat = -0.5 * M_PI;
				}

				nodes[ixNode].x = cos(dLon) * cos(dLat);
				nodes[ixNode].y = sin(dLon) * cos(dLat);
				nodes[ixNode].z = sin(dLat);

				ixNode++;
			}
		}

		// SCRIP does not reference a node table, so we must remove
		// coincident nodes.
		if (fRemoveCoincidentNodes) {
			Announce("Removing coincident nodes");
			RemoveCoincidentNodes();
		}

		// Output size
		Announce("Mesh size: Nodes [%i] Elements [%i]",
			nodes.size(), faces.size());

	// Input from a NetCDF Exodus file
	} else {

		// Get version number
		NcAtt * attVersion = ncFile.get_att("version");
		if (attVersion == NULL) {
			_EXCEPTION1("Exodus Grid file \"%s\" is missing attribute "
					"\"version\"", strFile.c_str());
		}
		if (attVersion->type() != ncFloat) {
			_EXCEPTIONT("Exodus Grid type is not of type float");
		}
		float flVersion = attVersion->as_float(0);

		// Number of nodes
		NcDim * dimNodes = ncFile.get_dim("num_nodes");
		if (dimNodes == NULL) {
			_EXCEPTION1("Exodus Grid file \"%s\" is missing dimension "
					"\"num_nodes\"", strFile.c_str());
		}
		int nNodeCount = dimNodes->size();

		// Determine number of blocks
		NcDim * dimElementBlocks = ncFile.get_dim("num_el_blk");
		if (dimElementBlocks == NULL) {
			_EXCEPTION1("Exodus Grid file \"%s\" is missing dimension "
					"\"num_el_blk\"", strFile.c_str());
		}
		int nElementBlocks = dimElementBlocks->size();

		// Total number of elements
		NcDim * dimElements = ncFile.get_dim("num_elem");
		if (dimElements == NULL) {
			_EXCEPTION1("Exodus Grid file \"%s\" is missing dimension "
					"\"num_elem\"", strFile.c_str());
		}
		int nTotalElementCount = dimElements->size();

		// Output size
		Announce("Mesh size: Nodes [%i] Elements [%i]",
			nNodeCount, nTotalElementCount);

		// Allocate faces
		faces.resize(nTotalElementCount);

		// Loop over all blocks
		for (int n = 0; n < nElementBlocks; n++) {

			// Determine number of nodes per element in this block
			char szNodesPerElement[ParamLenString];
			snprintf(szNodesPerElement, ParamLenString, "num_nod_per_el%i", n+1);
			NcDim * dimNodesPerElement = ncFile.get_dim(szNodesPerElement);
			if (dimNodesPerElement == NULL) {
				_EXCEPTION2("Exodus Grid file \"%s\" is missing dimension "
					"\"%s\"", strFile.c_str(), szNodesPerElement);
			}
			int nNodesPerElement = dimNodesPerElement->size();

			// Number of elements in block
			char szElementsInBlock[ParamLenString];
			snprintf(szElementsInBlock, ParamLenString, "num_el_in_blk%i", n+1);

			NcDim * dimBlockElements = ncFile.get_dim(szElementsInBlock);
			if (dimBlockElements == NULL) {
				_EXCEPTION2("Exodus Grid file \"%s\" is missing dimension "
						"\"%s\"", strFile.c_str(), szElementsInBlock);
			}
			int nElementCount = dimBlockElements->size();

			// Variables for each face
			DataArray2D<int> iConnect(nElementCount, nNodesPerElement);
			DataArray1D<int> iGlobalId(nElementCount);
			DataArray2D<int> iEdgeType(nElementCount, nNodesPerElement);

			DataArray1D<int> iParentA(nElementCount);
			DataArray1D<int> iParentB(nElementCount);

			// Load in nodes for all elements in this block
			char szConnect[ParamLenString];
			snprintf(szConnect, ParamLenString, "connect%i", n+1);

			NcVar * varConnect = ncFile.get_var(szConnect);
			if (varConnect == NULL) {
				_EXCEPTION2("Exodus Grid file \"%s\" is missing variable "
						"\"%s\"", strFile.c_str(), szConnect);
			}

			varConnect->set_cur(0, 0);
			varConnect->get(
				&(iConnect[0][0]),
				nElementCount,
				nNodesPerElement);

			// Earlier version didn't have global_id
			if (flVersion == 4.98f) {
				for (int i = 0; i < nElementCount; i++) {
					iGlobalId[i] = i + 1;
				}

			// Load in global id for all elements in this block
			} else {
				char szGlobalId[ParamLenString];
				snprintf(szGlobalId, ParamLenString, "global_id%i", n+1);

				NcVar * varGlobalId = ncFile.get_var(szGlobalId);
				if (varGlobalId == NULL) {
					_EXCEPTION2("Exodus Grid file \"%s\" is missing variable "
							"\"%s\"", strFile.c_str(), szGlobalId);
				}

				varGlobalId->set_cur((long)0);
				varGlobalId->get(
					&(iGlobalId[0]),
					nElementCount);
			}

			// Load in edge type for all elements in this block
			char szEdgeType[ParamLenString];
			if (flVersion == 4.98f) {
				snprintf(szEdgeType, ParamLenString, "edge_type");
			} else {
				snprintf(szEdgeType, ParamLenString, "edge_type%i", n+1);
			}

			NcVar * varEdgeType = ncFile.get_var(szEdgeType);
			if (varEdgeType != NULL) {
				varEdgeType->set_cur(0, 0);
				varEdgeType->get(
					&(iEdgeType[0][0]),
					nElementCount,
					nNodesPerElement);
			}

			// Load in parent from A grid for all elements in this block
			char szParentA[ParamLenString];
			if (flVersion == 4.98f) {
				snprintf(szParentA, ParamLenString, "face_source_1");
			} else {
				snprintf(szParentA, ParamLenString, "el_parent_a%i", n+1);
			}

			NcVar * varParentA = ncFile.get_var(szParentA);
			if ((varParentA == NULL) && (vecSourceFaceIx.size() != 0)) {
				_EXCEPTION2("Exodus Grid file \"%s\" is missing variable "
						"\"%s\"", strFile.c_str(), szParentA);

			} else if (varParentA != NULL) {
				if (vecSourceFaceIx.size() == 0) {
					vecSourceFaceIx.resize(nTotalElementCount);
				}

				varParentA->set_cur((long)0);
				varParentA->get(
					&(iParentA[0]),
					nElementCount);
			}

			// Load in parent from A grid for all elements in this block
			char szParentB[ParamLenString];
			if (flVersion == 4.98f) {
				snprintf(szParentB, ParamLenString, "face_source_2");
			} else {
				snprintf(szParentB, ParamLenString, "el_parent_b%i", n+1);
			}

			NcVar * varParentB = ncFile.get_var(szParentB);
			if ((varParentB == NULL) && (vecTargetFaceIx.size() != 0)) {
				_EXCEPTION2("Exodus Grid file \"%s\" is missing variable "
						"\"%s\"", strFile.c_str(), szParentB);

			} else if (varParentB != NULL) {
				if (vecTargetFaceIx.size() == 0) {
					vecTargetFaceIx.resize(nTotalElementCount);
				}

				varParentB->set_cur((long)0);
				varParentB->get(
					&(iParentB[0]),
					nElementCount);
			}

			// Put local data into global structures
			for (int i = 0; i < nElementCount; i++) {
				if (iGlobalId[i] - 1 >= nTotalElementCount) {
					_EXCEPTION2("global_id %i out of range [1,%i]",
						iGlobalId[i], nTotalElementCount);
				}
				faces[iGlobalId[i]-1] = Face(nNodesPerElement);
				for (int k = 0; k < nNodesPerElement; k++) {
					faces[iGlobalId[i] - 1].SetNode(k, iConnect[i][k] - 1);
					faces[iGlobalId[i] - 1].edges[k].type = 
						static_cast<Edge::Type>(iEdgeType[i][k]);
				}

				if (vecSourceFaceIx.size() != 0) {
					vecSourceFaceIx[iGlobalId[i] - 1] = iParentA[i] - 1;
				}

				if (vecTargetFaceIx.size() != 0) {
					vecTargetFaceIx[iGlobalId[i] - 1] = iParentB[i] - 1;
				}
			}
		}

		// Earlier version had incorrect parent indexing
		if (flVersion == 4.98f) {
			if (vecSourceFaceIx.size() != 0) {
				for (int i = 0; i < nTotalElementCount; i++) {
					vecSourceFaceIx[i]++;
				}
			}
			if (vecTargetFaceIx.size() != 0) {
				for (int i = 0; i < nTotalElementCount; i++) {
					vecTargetFaceIx[i]++;
				}
			}
		}

		// Load in node array
		{
			nodes.resize(nNodeCount);

			NcVar * varNodes = ncFile.get_var("coord");
			if (varNodes == NULL) {
				_EXCEPTION1("Exodus Grid file \"%s\" is missing variable "
						"\"coord\"", strFile.c_str());
			}

			DataArray2D<double> dNodeCoords(3, nNodeCount);

			// Load in node array
			varNodes->set_cur(0, 0);
			varNodes->get(&(dNodeCoords[0][0]), 3, nNodeCount);

			for (int i = 0; i < nNodeCount; i++) {
				nodes[i].x = static_cast<Real>(dNodeCoords[0][i]);
				nodes[i].y = static_cast<Real>(dNodeCoords[1][i]);
				nodes[i].z = static_cast<Real>(dNodeCoords[2][i]);
			}
		}

		// Remove coincident nodes.
		RemoveCoincidentNodes();
	}
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::RemoveZeroEdges() {

	// Remove zero edges from all Faces
	for (int i = 0; i < faces.size(); i++) {
		faces[i].RemoveZeroEdges();
	}
}

///////////////////////////////////////////////////////////////////////////////

void Mesh::Validate() const {

	// Valid that Nodes have magnitude 1
	for (int i = 0; i < nodes.size(); i++) {
		double dMag = nodes[i].Magnitude();

		if (fabs(dMag - 1.0) > ReferenceTolerance) {
			_EXCEPTION5("Mesh validation failed: "
				"Node[%i] of non-unit magnitude detected (%1.10e, %1.10e, %1.10e) = %1.10e",
				i, nodes[i].x, nodes[i].y, nodes[i].z, dMag);

		}
	}

	// Validate that edges are oriented counter-clockwise
	for (int i = 0; i < faces.size(); i++) {
		const Face & face = faces[i];

		const int nEdges = face.edges.size();

		for (int j = 0; j < nEdges; j++) {

			// Check for zero edges
			for(;;) {
				if (face.edges[j][0] == face.edges[j][1]) {
					j++;
				} else {
					break;
				}
				if (j == nEdges) {
					break;
				}
			}

			if (j == nEdges) {
				break;
			}

			// Find the next non-zero edge
			int jNext = (j + 1) % nEdges;

			for(;;) {
				if (face.edges[jNext][0] == face.edges[jNext][1]) {
					jNext++;
				} else {
					break;
				}
				if (jNext == nEdges) {
					jNext = 0;
				}
				if (jNext == ((j + 1) % nEdges)) {
					_EXCEPTIONT("Mesh validation failed: "
						"No edge information on Face");
				}
			}

			// Get edges
			const Edge & edge0 = face.edges[j];
			const Edge & edge1 = face.edges[(j + 1) % nEdges];

			if (edge0[1] != edge1[0]) {
				_EXCEPTIONT("Mesh validation failed: Edge cyclicity error");
			}

			const Node & node0 = nodes[edge0[0]];
			const Node & node1 = nodes[edge0[1]];
			const Node & node2 = nodes[edge1[1]];

			// Vectors along edges
			Node nodeD1 = node0 - node1;
			Node nodeD2 = node2 - node1;

			// Compute cross-product
			Node nodeCross(CrossProduct(nodeD1, nodeD2));

			// Dot cross product with radial vector
			Real dDot = DotProduct(node1, nodeCross);
/*
#ifdef USE_EXACT_ARITHMETIC
			FixedPoint dDotX = DotProductX(node1, nodeCross);

			printf("%1.15e : ", nodeCross.x); nodeCross.fx.Print(); printf("\n");

			if (fabs(nodeCross.x - nodeCross.fx.ToReal()) > ReferenceTolerance) {
				printf("X0: %1.15e : ", node0.x); node0.fx.Print(); printf("\n");
				printf("Y0: %1.15e : ", node0.y); node0.fy.Print(); printf("\n");
				printf("Z0: %1.15e : ", node0.z); node0.fz.Print(); printf("\n");
				printf("X1: %1.15e : ", node1.x); node1.fx.Print(); printf("\n");
				printf("Y1: %1.15e : ", node1.y); node1.fy.Print(); printf("\n");
				printf("Z1: %1.15e : ", node1.z); node1.fz.Print(); printf("\n");
				printf("X2: %1.15e : ", node2.x); node2.fx.Print(); printf("\n");
				printf("Y2: %1.15e : ", node2.y); node2.fy.Print(); printf("\n");
				printf("Z2: %1.15e : ", node2.z); node2.fz.Print(); printf("\n");

				printf("X1: %1.15e : ", nodeD1.x); nodeD1.fx.Print(); printf("\n");
				printf("Y1: %1.15e : ", nodeD1.y); nodeD1.fy.Print(); printf("\n");
				printf("Z1: %1.15e : ", nodeD1.z); nodeD1.fz.Print(); printf("\n");
				printf("X2: %1.15e : ", nodeD2.x); nodeD2.fx.Print(); printf("\n");
				printf("Y2: %1.15e : ", nodeD2.y); nodeD2.fy.Print(); printf("\n");
				printf("Z2: %1.15e : ", nodeD2.z); nodeD2.fz.Print(); printf("\n");
				_EXCEPTIONT("FixedPoint mismatch (X)");
			}
			if (fabs(nodeCross.y - nodeCross.fy.ToReal()) > ReferenceTolerance) {
				_EXCEPTIONT("FixedPoint mismatch (Y)");
			}
			if (fabs(nodeCross.z - nodeCross.fz.ToReal()) > ReferenceTolerance) {
				_EXCEPTIONT("FixedPoint mismatch (Z)");
			}

#endif
*/
			if (dDot > 0.0) {
				printf("\nError detected (orientation):\n");
				printf("  Face %i, Edge %i, Orientation %1.5e\n",
					i, j, dDot);

				printf("  (x,y,z):\n");
				printf("    n0: %1.5e %1.5e %1.5e\n", node0.x, node0.y, node0.z);
				printf("    n1: %1.5e %1.5e %1.5e\n", node1.x, node1.y, node1.z);
				printf("    n2: %1.5e %1.5e %1.5e\n", node2.x, node2.y, node2.z);

				Real dR0 = sqrt(
					node0.x * node0.x + node0.y * node0.y + node0.z * node0.z);
				Real dLat0 = asin(node0.z / dR0);
				Real dLon0 = atan2(node0.y, node0.x);

				Real dR1 = sqrt(
					node1.x * node1.x + node1.y * node1.y + node1.z * node1.z);
				Real dLat1 = asin(node1.z / dR1);
				Real dLon1 = atan2(node1.y, node1.x);

				Real dR2 = sqrt(
					node2.x * node2.x + node2.y * node2.y + node2.z * node2.z);
				Real dLat2 = asin(node2.z / dR2);
				Real dLon2 = atan2(node2.y, node2.x);

				printf("  (lambda, phi):\n");
				printf("    n0: %1.5e %1.5e\n", dLon0, dLat0);
				printf("    n1: %1.5e %1.5e\n", dLon1, dLat1);
				printf("    n2: %1.5e %1.5e\n", dLon2, dLat2);

				printf("  X-Product:\n");
				printf("    %1.5e %1.5e %1.5e\n",
					nodeCross.x, nodeCross.y, nodeCross.z);

				_EXCEPTIONT(
					"Mesh validation failed: Clockwise or concave face detected");
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// General purpose functions
///////////////////////////////////////////////////////////////////////////////

bool IsPositivelyOrientedEdge(
	const Node & nodeBegin,
	const Node & nodeEnd
) {
	const Real Tolerance = ReferenceTolerance;

	if ((fabs(nodeBegin.x - nodeEnd.x) < Tolerance) &&
		(fabs(nodeBegin.y - nodeEnd.y) < Tolerance) &&
		(fabs(nodeBegin.z - nodeEnd.z) < Tolerance)
	) {
		_EXCEPTIONT("Latitude line of zero length");
	}

	// Both nodes in positive y half-plane
	if ((nodeBegin.y >= 0.0) && (nodeEnd.y >= 0.0)) {
		if (nodeEnd.x < nodeBegin.x) {
			return true;
		} else {
			return false;
		}

	// Both nodes in negative y half-plane
	} else if ((nodeBegin.y <= 0.0) && (nodeEnd.y <= 0.0)) {
		if (nodeEnd.x > nodeBegin.x) {
			return true;
		} else {
			return false;
		}

	// Both nodes in positive x half-plane
	} else if ((nodeBegin.x >= 0.0) && (nodeEnd.x >= 0.0)) {
		if (nodeEnd.y > nodeBegin.y) {
			return true;
		} else {
			return false;
		}

	// Both nodes in negative x half-plane
	} else if ((nodeBegin.x <= 0.0) && (nodeEnd.x <= 0.0)) {
		if (nodeEnd.y < nodeBegin.y) {
			return true;
		} else {
			return false;
		}

	// Arc length too large
	} else {
		_EXCEPTIONT("Arc length too large to determine orientation.");
	}
}

///////////////////////////////////////////////////////////////////////////////

void GetLocalDirection(
	const Node & nodeBegin,
	const Node & nodeEnd,
	const Node & nodeRef,
	const Edge::Type edgetype,
	Node & nodeDir
) {

	// Direction along a great circle arc
	if (edgetype == Edge::Type_GreatCircleArc) {

		// Cartesian direction
		nodeDir = nodeEnd - nodeBegin;

		// Project onto surface of the sphere
		Real dDotDirBegin = DotProduct(nodeDir, nodeRef);
		Real dNormNodeBegin = DotProduct(nodeRef, nodeRef);

		nodeDir.x -= dDotDirBegin / dNormNodeBegin * nodeRef.x;
		nodeDir.y -= dDotDirBegin / dNormNodeBegin * nodeRef.y;
		nodeDir.z -= dDotDirBegin / dNormNodeBegin * nodeRef.z;

	// Direction along a line of constant latitude
	} else if (edgetype == Edge::Type_ConstantLatitude) {
		nodeDir.z = 0.0;

		if (IsPositivelyOrientedEdge(nodeBegin, nodeEnd)) {
			nodeDir.x = - nodeBegin.y;
			nodeDir.y = + nodeBegin.x;
		} else {
			nodeDir.x = + nodeBegin.y;
			nodeDir.y = - nodeBegin.x;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void GetLocalDirection(
	const Node & nodeBegin,
	const Node & nodeEnd,
	const Edge::Type edgetype,
	Node & nodeDir
) {

	// Direction along a great circle arc
	if (edgetype == Edge::Type_GreatCircleArc) {

		// Cartesian direction
		nodeDir = nodeEnd - nodeBegin;

		// Project onto surface of the sphere
		Real dDotDirBegin   = DotProduct(nodeDir, nodeBegin);
		Real dNormNodeBegin = DotProduct(nodeBegin, nodeBegin);

		nodeDir.x -= dDotDirBegin / dNormNodeBegin * nodeBegin.x;
		nodeDir.y -= dDotDirBegin / dNormNodeBegin * nodeBegin.y;
		nodeDir.z -= dDotDirBegin / dNormNodeBegin * nodeBegin.z;

	// Direction along a line of constant latitude
	} else if (edgetype == Edge::Type_ConstantLatitude) {
		nodeDir.z = 0.0;

		if (IsPositivelyOrientedEdge(nodeBegin, nodeEnd)) {
			nodeDir.x = - nodeBegin.y;
			nodeDir.y = + nodeBegin.x;
		} else {
			nodeDir.x = + nodeBegin.y;
			nodeDir.y = - nodeBegin.x;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void EqualizeCoincidentNodes(
	const Mesh & meshFirst,
	Mesh & meshSecond
) {
	_EXCEPTION();
/*
	// Sort nodes
	std::map<Node, int> setSortedFirstNodes;
	for (int i = 0; i < meshFirst.nodes.size(); i++) {
		setSortedFirstNodes.insert(
			std::pair<Node, int>(meshFirst.nodes[i], i));
	}

	// For each node in meshSecond determine if a corresponding node
	// exists in meshFirst.
	for (int i = 0; i < meshSecond.nodes.size(); i++) {
		std::map<Node, int>::const_iterator iter =
			setSortedFirstNodes.find(meshSecond.nodes[i]);

		if (iter != setSortedFirstNodes.end()) {
			meshSecond.nodes[i] = iter->first;
		}
	}
*/
}

///////////////////////////////////////////////////////////////////////////////
/*
void EqualizeCoincidentNodes(
	Mesh & mesh
) {
	int nCoincidentNodes = 0;

	// Sort nodes
	std::map<Node, int> mapSortedNodes;
	for (int i = 0; i < mesh.nodes.size(); i++) {
		std::map<Node, int>::const_iterator iter =
			mapSortedNodes.find(mesh.nodes[i]);

		if (iter != mapSortedNodes.end()) {
			nCoincidentNodes++;
			mesh.nodes[i] = iter->first;
		} else {
			mapSortedNodes.insert(
				std::pair<Node, int>(mesh.nodes[i], i));
		}
	}

	printf("Coincident nodes: %i\n", nCoincidentNodes);
}
*/
///////////////////////////////////////////////////////////////////////////////

int BuildCoincidentNodeVector(
	const Mesh & meshFirst,
	const Mesh & meshSecond,
	std::vector<int> & vecSecondToFirstCoincident
) {
	_EXCEPTION();
/*
	int nCoincidentNodes = 0;

	// Sort nodes
	std::map<Node, int> setSortedFirstNodes;
	for (int i = 0; i < meshFirst.nodes.size(); i++) {
		setSortedFirstNodes.insert(
			std::pair<Node, int>(meshFirst.nodes[i], i));
	}

	// Resize array
	vecSecondToFirstCoincident.resize(meshSecond.nodes.size(), InvalidNode);

	// For each node in meshSecond determine if a corresponding node
	// exists in meshFirst.
	for (int i = 0; i < meshSecond.nodes.size(); i++) {
		std::map<Node, int>::const_iterator iter =
			setSortedFirstNodes.find(meshSecond.nodes[i]);

		if (iter != setSortedFirstNodes.end()) {
			vecSecondToFirstCoincident[i] = iter->second;
			nCoincidentNodes++;
		}
	}

	return nCoincidentNodes;
*/
}

///////////////////////////////////////////////////////////////////////////////

Real CalculateFaceAreaQuadratureMethod(
	const Face & face,
	const NodeVector & nodes
) {
	int nTriangles = face.edges.size() - 2;

	const int nOrder = 6;

	DataArray1D<double> dG;
	DataArray1D<double> dW;
	GaussQuadrature::GetPoints(nOrder, 0.0, 1.0, dG, dW);

	double dFaceArea = 0.0;

	// Loop over all sub-triangles of this Face
	for (int j = 0; j < nTriangles; j++) {

		// Calculate the area of the modified Face
		Node node1 = nodes[face[0]];
		Node node2 = nodes[face[j+1]];
		Node node3 = nodes[face[j+2]];

		// Calculate area at quadrature node
		for (int p = 0; p < dW.GetRows(); p++) {
		for (int q = 0; q < dW.GetRows(); q++) {

			double dA = dG[p];
			double dB = dG[q];

			Node dF(
				(1.0 - dB) * ((1.0 - dA) * node1.x + dA * node2.x) + dB * node3.x,
				(1.0 - dB) * ((1.0 - dA) * node1.y + dA * node2.y) + dB * node3.y,
				(1.0 - dB) * ((1.0 - dA) * node1.z + dA * node2.z) + dB * node3.z);

			Node dDaF(
				(1.0 - dB) * (node2.x - node1.x),
				(1.0 - dB) * (node2.y - node1.y),
				(1.0 - dB) * (node2.z - node1.z));

			Node dDbF(
				- (1.0 - dA) * node1.x - dA * node2.x + node3.x,
				- (1.0 - dA) * node1.y - dA * node2.y + node3.y,
				- (1.0 - dA) * node1.z - dA * node2.z + node3.z);

			double dR = sqrt(dF.x * dF.x + dF.y * dF.y + dF.z * dF.z);

			Node dDaG(
				dDaF.x * (dF.y * dF.y + dF.z * dF.z)
					- dF.x * (dDaF.y * dF.y + dDaF.z * dF.z),
				dDaF.y * (dF.x * dF.x + dF.z * dF.z)
					- dF.y * (dDaF.x * dF.x + dDaF.z * dF.z),
				dDaF.z * (dF.x * dF.x + dF.y * dF.y)
					- dF.z * (dDaF.x * dF.x + dDaF.y * dF.y));

			Node dDbG(
				dDbF.x * (dF.y * dF.y + dF.z * dF.z)
					- dF.x * (dDbF.y * dF.y + dDbF.z * dF.z),
				dDbF.y * (dF.x * dF.x + dF.z * dF.z)
					- dF.y * (dDbF.x * dF.x + dDbF.z * dF.z),
				dDbF.z * (dF.x * dF.x + dF.y * dF.y)
					- dF.z * (dDbF.x * dF.x + dDbF.y * dF.y));

			double dDenomTerm = 1.0 / (dR * dR * dR);

			dDaG.x *= dDenomTerm;
			dDaG.y *= dDenomTerm;
			dDaG.z *= dDenomTerm;

			dDbG.x *= dDenomTerm;
			dDbG.y *= dDenomTerm;
			dDbG.z *= dDenomTerm;
/*
			Node node;
			Node dDx1G;
			Node dDx2G;

			ApplyLocalMap(
				faceQuad,
				nodes,
				dG[p],
				dG[q],
				node,
				dDx1G,
				dDx2G);
*/
			// Cross product gives local Jacobian
			Node nodeCross = CrossProduct(dDaG, dDbG);

			double dJacobian = sqrt(
				  nodeCross.x * nodeCross.x
				+ nodeCross.y * nodeCross.y
				+ nodeCross.z * nodeCross.z);

			//dFaceArea += 2.0 * dW[p] * dW[q] * (1.0 - dG[q]) * dJacobian;
			
			dFaceArea += dW[p] * dW[q] * dJacobian;
		}
		}
	}

	return dFaceArea;
}

///////////////////////////////////////////////////////////////////////////////

Real CalculateFaceArea(
	const Face & face,
	const NodeVector & nodes
) {
	return CalculateFaceAreaQuadratureMethod(face, nodes);
}

///////////////////////////////////////////////////////////////////////////////

