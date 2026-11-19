/** 
 * @file llstl.h
 * @brief helper object & functions for use with the stl.
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
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

#ifndef LL_LLSTL_H
#define LL_LLSTL_H

#include "stdtypes.h"
#include <functional>
#include <algorithm>
#include <map>
#include <vector>
#include <list>
#include <set>
#include <typeindex>
#include <typeinfo>

// Use to compare the contents of two pointers (e.g. std::string*)
template <typename T>
struct compare_pointer_contents
{
	typedef const T* Tptr;
	bool operator()(const Tptr& a, const Tptr& b) const
	{
		return *a < *b;
	}
};

// DeletePointer is a simple helper for deleting all pointers in a container.
// The general form is:
//
//  std::for_each(cont.begin(), cont.end(), DeletePointer());
//  somemap.clear();
//
// Don't forget to clear()!

struct DeletePointer
{
	template<typename T> void operator()(T* ptr) const
	{
		delete ptr;
	}
};

// DeletePairedPointer is a simple helper for deleting all pointers in a map.
// The general form is:
//
//  std::for_each(somemap.begin(), somemap.end(), DeletePairedPointer());
//  somemap.clear();		// Don't leave dangling pointers around

struct DeletePairedPointer
{
	template<typename T> void operator()(T &ptr) const
	{
		delete ptr.second;
		ptr.second = NULL;
	}
};


// Alternate version of the above so that has a more cumbersome
// syntax, but it can be used with compositional functors.
// NOTE: The functor retuns a bool because msdev bombs during the
// composition if you return void. Once we upgrade to a newer
// compiler, the second unary_function template parameter can be set
// to void.
//
// Here's a snippet showing how you use this object:
//
// typedef std::map<int, widget*> map_type;
// map_type widget_map;
// ... // add elements
// // delete them all
// for_each(widget_map.begin(),
//          widget_map.end(),
//          llcompose1(DeletePointerFunctor<widget>(),
//                     llselect2nd<map_type::value_type>()));

template<typename T>
struct DeletePointerFunctor
{
	bool operator()(T* ptr) const
	{
		delete ptr;
		return true;
	}
};

template<typename T, typename ALLOC>
void delete_and_clear(std::list<T*, ALLOC>& list)
{
	std::for_each(list.begin(), list.end(), DeletePointer());
	list.clear();
}

template<typename T, typename ALLOC>
void delete_and_clear(std::vector<T*, ALLOC>& vector)
{
	std::for_each(vector.begin(), vector.end(), DeletePointer());
	vector.clear();
}

template<typename T, typename COMPARE, typename ALLOC>
void delete_and_clear(std::set<T*, COMPARE, ALLOC>& set)
{
	std::for_each(set.begin(), set.end(), DeletePointer());
	set.clear();
}

template<typename K, typename V, typename COMPARE, typename ALLOC>
void delete_and_clear(std::map<K, V*, COMPARE, ALLOC>& map)
{
	std::for_each(map.begin(), map.end(), DeletePairedPointer());
	map.clear();
}

template<typename T>
void delete_and_clear(T*& ptr)
{
	delete ptr;
	ptr = NULL;
}


template<typename T>
void delete_and_clear_array(T*& ptr)
{
	delete[] ptr;
	ptr = NULL;
}

// Simple function to help with finding pointers in maps.
// For example:
// 	typedef  map_t;
//  std::map<int, const char*> foo;
//	foo[18] = "there";
//	foo[2] = "hello";
// 	const char* bar = get_ptr_in_map(foo, 2); // bar -> "hello"
//  const char* baz = get_ptr_in_map(foo, 3); // baz == NULL
template <typename T>
inline typename T::mapped_type get_ptr_in_map(const T& inmap, typename T::key_type const& key)
{
    auto iter = inmap.find(key);
	if(iter == inmap.end())
	{
        return nullptr;
	}
	else
	{
		return iter->second;
	}
};

// helper function which returns true if key is in inmap.
template <typename T>
inline bool is_in_map(const T& inmap, typename T::key_type const& key)
{
	if(inmap.find(key) == inmap.end())
	{
		return false;
	}
	else
	{
		return true;
	}
}

// Similar to get_ptr_in_map, but for any type with a valid T(0) constructor.
// To replace LLSkipMap getIfThere, use:
//   get_if_there(map, key, 0)
// WARNING: Make sure default_value (generally 0) is not a valid map entry!
template <typename T>
inline typename T::mapped_type get_if_there(const T& inmap, typename T::key_type const& key, typename T::mapped_type default_value)
{
    auto iter = inmap.find(key);
	if(iter == inmap.end())
	{
		return default_value;
	}
	else
	{
		return iter->second;
	}
};

// Useful for replacing the removeObj() functionality of LLDynamicArray
// Example:
//  for (std::vector<T>::iterator iter = mList.begin(); iter != mList.end(); )
//  {
//    if ((*iter)->isMarkedForRemoval())
//      iter = vector_replace_with_last(mList, iter);
//    else
//      ++iter;
//  }
template <typename T>
inline typename std::vector<T>::iterator vector_replace_with_last(std::vector<T>& invec, typename std::vector<T>::iterator iter)
{
	typename std::vector<T>::iterator last = invec.end(); --last;
	if (iter == invec.end())
	{
		return iter;
	}
	else if (iter == last)
	{
		invec.pop_back();
		return invec.end();
	}
	else
	{
		*iter = *last;
		invec.pop_back();
		return iter;
	}
};

// Example:
//   vector_replace_with_last(mList, x);
template <typename T>
inline bool vector_replace_with_last(std::vector<T>& invec, const T& val)
{
	typename std::vector<T>::iterator iter = std::find(invec.begin(), invec.end(), val);
	if (iter != invec.end())
	{
		typename std::vector<T>::iterator last = invec.end(); --last;
		*iter = *last;
		invec.pop_back();
		return true;
	}
	return false;
}

// Append N elements to the vector and return a pointer to the first new element.
template <typename T>
inline T* vector_append(std::vector<T>& invec, S32 N)
{
    auto sz = invec.size();
	invec.resize(sz+N);
	return &(invec[sz]);
}

/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1996-1998
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */


// helper to deal with the fact that MSDev does not package
// select... with the stl. Look up usage on the sgi website.

template <class _Pair>
struct _LLSelect1st
{
  const auto& operator()(const _Pair& __x) const {
    return __x.first;
  }
};

template <class _Pair>
struct _LLSelect2nd
{
  const auto& operator()(const _Pair& __x) const {
    return __x.second;
  }
};

template <class _Pair> struct llselect1st : public _LLSelect1st<_Pair> {};
template <class _Pair> struct llselect2nd : public _LLSelect2nd<_Pair> {};

// helper to deal with the fact that MSDev does not package
// compose... with the stl. Look up usage on the sgi website.

template <class _Operation1, class _Operation2>
class ll_unary_compose
{
protected:
  _Operation1 __op1;
  _Operation2 __op2;
public:
  ll_unary_compose(const _Operation1& __x, const _Operation2& __y)
    : __op1(__x), __op2(__y) {}
  template <typename _Op2Arg>
  auto
  operator()(const _Op2Arg& __x) const {
    return __op1(__op2(__x));
  }
};

template <class _Operation1, class _Operation2>
inline ll_unary_compose<_Operation1,_Operation2>
llcompose1(const _Operation1& __op1, const _Operation2& __op2)
{
  return ll_unary_compose<_Operation1,_Operation2>(__op1, __op2);
}

/**
 * Compare std::type_info* pointers a la std::less. We break this out as a
 * separate function for use in two different std::less specializations.
 */
inline
bool before(const std::type_info* lhs, const std::type_info* rhs)
{
    return lhs->before(*rhs);
}

/**
 * Specialize std::less<std::type_info*> to use std::type_info::before().
 * See MAINT-1175. It is NEVER a good idea to directly compare std::type_info*
 * because, on Linux, you might get different std::type_info* pointers for the
 * same type (from different load modules)!
 */
namespace std
{
	template <>
	struct less<const std::type_info*>
	{
		bool operator()(const std::type_info* lhs, const std::type_info* rhs) const
		{
			return before(lhs, rhs);
		}
	};

	template <>
	struct less<std::type_info*>
	{
		bool operator()(std::type_info* lhs, std::type_info* rhs) const
		{
			return before(lhs, rhs);
		}
	};
} // std


// Transparent string hashing helper for use with std::unordered_*
// std::unordered_map<std::string, val, ll::string_hash, std::equal_to<>>
namespace ll
{
    struct string_hash
    {
        using is_transparent = void;
        [[nodiscard]] size_t operator()(char const* rhs) const { return std::hash<std::string_view>{}(rhs); }
        [[nodiscard]] size_t operator()(std::string_view rhs) const { return std::hash<std::string_view>{}(rhs); }
        [[nodiscard]] size_t operator()(const std::string& rhs) const { return std::hash<std::string>{}(rhs); }
    };
} // namespace ll

// Specialize ostream for std::type_index to allow log output
inline std::ostream& operator<<(std::ostream& s, std::type_index type)
{
    s << type.name();
    return s;
}
#endif // LL_LLSTL_H
