/*
 *  Copyright (c) 2026, MafiaHub
 *
 *  This source code is licensed under the MIT-style license found in the
 *  license.txt file in the root directory of this source tree.
 */

/// \file
/// \brief Uniform grid spatial index over point entries with incremental
/// add/remove/move, intended as the server-side interest-management index.
///
/// Unlike GridSectorizer — whose compiled-in per-cell storage is append-only,
/// forcing consumers to Clear() and rebuild the whole grid to evict anything —
/// PointGridSectorizer keeps a per-entry record (cell + slot) in a
/// runtime-sized open-addressing table, so entries can be removed or moved
/// individually in amortized O(1), independent of the cell count and the total
/// entry count. Entries are points, not boxes: each entry occupies exactly one
/// cell, so GetEntries() never returns duplicates and callers do not need to
/// dedup (GridSectorizer could return one entry once per cell its box spanned).

#ifndef MAFIANET_POINT_GRID_SECTORIZER_H
#define MAFIANET_POINT_GRID_SECTORIZER_H

#include "Export.h"
#include "memoryoverride.h"
#include "DS_List.h"

namespace MafiaNet
{

/// \brief Spatial grid over point entries with O(1) incremental updates.
/// \details One entry per pointer: AddEntry() and MoveEntry() are the same
/// upsert operation, so position writes can be forwarded to the grid blindly
/// without tracking membership. All positions (entries and query rectangles)
/// are clamped to the world bounds given to Init() — out-of-bounds, even
/// non-finite, positions land in the edge cells (NaN maps to the minimum edge
/// cell), they never assert, drop, or invoke undefined behavior. Before a
/// successful Init() every operation is a defined no-op. Queries return the
/// contents of every cell overlapping the rectangle, i.e. a cell-granularity
/// superset of the exact matches; callers post-filter, as with GridSectorizer.
/// Not thread-safe; intended for single-threaded use from the update loop.
class RAK_DLL_EXPORT PointGridSectorizer
{
public:
	PointGridSectorizer();
	~PointGridSectorizer();

	// Owns raw memory (cell lists and the entry-record table); a memberwise
	// copy would double-free it.
	PointGridSectorizer(const PointGridSectorizer&) = delete;
	PointGridSectorizer& operator=(const PointGridSectorizer&) = delete;

	/// (Re-)initializes the grid, discarding any current entries.
	/// \param[in] _cellWidth, _cellHeight Size of each cell in world units
	/// \param[in] minX, minY, maxX, maxY World bounds; positions outside clamp to the edge cells
	/// \return False — leaving the grid inert (all operations no-op) until a
	/// valid re-Init — if the cell sizes or bounds are not positive and finite,
	/// or the resulting cell count would not fit in an int.
	bool Init(const float _cellWidth, const float _cellHeight, const float minX, const float minY, const float maxX, const float maxY);

	/// Adds a point entry at (x,y), clamped to the world bounds. Amortized O(1).
	/// If \a entry is already in the grid this relocates it (same as MoveEntry).
	/// \pre \a entry must be non-null (null is rejected as a no-op).
	/// \return True if the entry was inserted or changed cell, false if it
	/// stayed in its current cell (or the grid is uninitialized / entry null).
	bool AddEntry(void *entry, const float x, const float y);

	/// Removes an entry. O(1) via swap-remove within its cell.
	/// \return True if the entry was in the grid, false if absent (no-op).
	bool RemoveEntry(void *entry);

	/// Moves an entry to (x,y), clamped to the world bounds. Amortized O(1);
	/// if the new position is in the entry's current cell this is a cheap
	/// early-out (the hot case for small per-tick movement). Inserts the entry
	/// if absent.
	/// \return True if the entry was inserted or changed cell — the signal an
	/// event-driven consumer needs to recompute interest sets — false if it
	/// stayed in its current cell (or the grid is uninitialized / entry null).
	bool MoveEntry(void *entry, const float x, const float y);

	/// Resets \a intersectionList (size 0, capacity kept for reuse) and fills
	/// it with every entry in the cells overlapping the rectangle, each exactly
	/// once. O(cells overlapped + entries returned).
	void GetEntries(DataStructures::List<void*> &intersectionList, const float minX, const float minY, const float maxX, const float maxY) const;

	/// Returns whether the entry is currently in the grid. O(1).
	bool HasEntry(void *entry) const;

	/// Number of entries in the grid. O(1).
	unsigned int Size(void) const;

	/// Removes all entries; the grid stays initialized and every allocation is
	/// kept for reuse. O(cell count + record-table capacity).
	void Clear(void);

protected:
	/// \internal Where an entry lives. Doubles as an open-addressing table
	/// slot: a null \a entry marks the slot empty.
	struct EntryRecord
	{
		void *entry;
		int cellIndex;
		unsigned int slotIndex;
	};

	EntryRecord* FindRecord(void *entry) const;
	void InsertRecord(void *entry, const int cellIndex, const unsigned int slotIndex);
	void EraseRecord(EntryRecord *record);
	void GrowRecordTable(void);

	unsigned int PushIntoCell(void *entry, const int cellIndex);
	void RemoveFromCell(const EntryRecord &record);

	int WorldToCellXClamped(const float input) const;
	int WorldToCellYClamped(const float input) const;
	int WorldToCellIndexClamped(const float x, const float y) const;

	float cellOriginX, cellOriginY;
	float invCellWidth, invCellHeight;
	int gridCellWidthCount, gridCellHeightCount;

	/// Per-cell unordered entry lists; removal swaps the last element into the
	/// freed slot, so order within a cell is not meaningful.
	DataStructures::List<void*> *grid;

	/// Open-addressing record table (linear probing, backward-shift deletion).
	/// Power-of-two capacity, grown with the entry count, never shrunk.
	EntryRecord *records;
	unsigned int recordCapacity;
	unsigned int recordCount;
};

} // namespace MafiaNet

#endif // MAFIANET_POINT_GRID_SECTORIZER_H
