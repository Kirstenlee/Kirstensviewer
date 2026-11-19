/** 
 * @file lldarray.h
 * @brief Wrapped std::vector for backward compatibility.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLDARRAY_H
#define LL_LLDARRAY_H

#include "llerror.h"

#include <algorithm>
#include <vector>

//--------------------------------------------------------
// LLIndexedVector
//--------------------------------------------------------

template <typename Type, typename Key, int BlockSize = 32> 
class LLIndexedVector
{
public:
	typedef typename std::vector<Type>::iterator iterator;
	typedef typename std::vector<Type>::const_iterator const_iterator;
	typedef typename std::vector<Type>::reverse_iterator reverse_iterator;
	typedef typename std::vector<Type>::const_reverse_iterator const_reverse_iterator;
	typedef typename std::vector<Type>::size_type size_type;
protected:
	std::vector<Type> mVector;
	// S24 : the keys, parallel to mVector. A message block holds a handful of variables (up to about 40),
	// where scanning a contiguous key array beats a std::map and allocates nothing per entry.
	std::vector<Key> mKeys;
	
public:
	LLIndexedVector() { mVector.reserve(BlockSize); mKeys.reserve(BlockSize); }
	
	iterator begin() { return mVector.begin(); }
	const_iterator begin() const { return mVector.begin(); }
	iterator end() { return mVector.end(); }
	const_iterator end() const { return mVector.end(); }

	reverse_iterator rbegin() { return mVector.rbegin(); }
	const_reverse_iterator rbegin() const { return mVector.rbegin(); }
	reverse_iterator rend() { return mVector.rend(); }
	const_reverse_iterator rend() const { return mVector.rend(); }

	void reset() { mVector.clear(); mKeys.clear(); }
	void reserve(size_type n) { mVector.reserve(n); mKeys.reserve(n); }
	bool empty() const { return mVector.empty(); }
	size_type size() const { return mVector.size(); }
	
	Type& operator[](const Key& k)
	{
		const auto iter = std::find(mKeys.begin(), mKeys.end(), k);
		if (iter == mKeys.end())
		{
			const auto n = mVector.size();
			mKeys.push_back(k);
			mVector.emplace_back();
			llassert(mVector.size() == mKeys.size());
			return mVector[n];
		}
		return mVector[iter - mKeys.begin()];
	}

	const_iterator find(const Key& k) const
	{
		const auto iter = std::find(mKeys.begin(), mKeys.end(), k);
		if (iter == mKeys.end())
		{
			return mVector.end();
		}
		return mVector.begin() + (iter - mKeys.begin());
	}
};

#endif
