//-----------------------------------------------------------------------------
// boost variant/detail/element_index.hpp header file
// See http://www.boost.org for updates, documentation, and revision history.
//-----------------------------------------------------------------------------
//
// Copyright (c) 2014-2022 Antony Polukhin
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_VARIANT_DETAIL_ELEMENT_INDEX_HPP
#define BOOST_VARIANT_DETAIL_ELEMENT_INDEX_HPP

#include <boost/config.hpp>
#include <boost/variant/recursive_wrapper_fwd.hpp>
#include <boost/variant/variant_fwd.hpp>

#include <boost/type_traits/remove_cv.hpp>
#include <boost/type_traits/remove_reference.hpp>
#include <boost/mpl/find_if.hpp>

namespace ddboost { namespace detail { namespace variant {

template <class VariantElement, class T>
struct variant_element_functor :
    ddboost::mpl::or_<
        ddboost::is_same<VariantElement, T>,
        ddboost::is_same<VariantElement, ddboost::recursive_wrapper<T> >,
        ddboost::is_same<VariantElement, T& >
    >
{};

template <class Types, class T>
struct element_iterator_impl :
    ddboost::mpl::find_if<
        Types,
        ddboost::mpl::or_<
            variant_element_functor<ddboost::mpl::_1, T>,
            variant_element_functor<ddboost::mpl::_1, typename ddboost::remove_cv<T>::type >
        >
    >
{};

template <class Variant, class T>
struct element_iterator :
    element_iterator_impl< typename Variant::types, typename ddboost::remove_reference<T>::type >
{};

template <class Variant, class T>
struct holds_element :
    ddboost::mpl::not_<
        ddboost::is_same<
            typename ddboost::mpl::end<typename Variant::types>::type,
            typename element_iterator<Variant, T>::type
        >
    >
{};


}}} // namespace ddboost::detail::variant

#endif // BOOST_VARIANT_DETAIL_ELEMENT_INDEX_HPP
