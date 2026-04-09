#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace tp {
namespace detail {

template <typename Func, typename... Args> constexpr Func for_each_arg(Func f, Args &&...args) {
  std::initializer_list<int>{f(std::forward<Args>(args), 0)...};
  return f;
}

template <typename Tuple, typename Func, std::size_t... I> constexpr Func for_each_impl(Tuple &&t, Func &&f, std::index_sequence<I...> is) {
  return std::initializer_list<int>{(std::forward<Func>(f)(std::get<I>(std::forward<Tuple>(t))), 0)...}, f;
}

template <typename... Ts, typename Function, size_t... Is> auto transform_impl(std::tuple<Ts...> const &inputs, Function function, std::index_sequence<Is...> is) {
  return std::tuple<std::result_of_t<Function(Ts)>...>{function(std::get<Is>(inputs))...};
}

template <typename... T, std::size_t... i> auto subtuple(const std::tuple<T...> &t, std::index_sequence<i...>) { return std::make_tuple(std::get<i>(t)...); }

// ZIP utilities
template <std::size_t I, typename... Tuples> using zip_tuple_at_index_t = std::tuple<std::tuple_element_t<I, std::decay_t<Tuples>>...>;
template <std::size_t I, typename... Tuples> zip_tuple_at_index_t<I, Tuples...> zip_tuple_at_index(Tuples &&...tuples) { return {std::get<I>(std::forward<Tuples>(tuples))...}; }
template <typename... Tuples, std::size_t... I> std::tuple<zip_tuple_at_index_t<I, Tuples...>...> tuple_zip_impl(Tuples &&...tuples, std::index_sequence<I...>) {
  return {zip_tuple_at_index<I>(std::forward<Tuples>(tuples)...)...};
}
}; // namespace detail

template <class Tuple, class F> constexpr decltype(auto) for_each(Tuple &&tuple, F &&f) {
  return []<std::size_t... I>(Tuple &&tuple, F &&f, std::index_sequence<I...>) {
    (f(std::get<I>(tuple)), ...);
    return f;
  }(std::forward<Tuple>(tuple), std::forward<F>(f), std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
}

template <typename... Ts, typename Function> auto transform(std::tuple<Ts...> const &inputs, Function function) {
  return detail::transform_impl(inputs, function, std::make_index_sequence<sizeof...(Ts)>{});
}

template <typename Tuple, typename Predicate> constexpr size_t find_if(Tuple &&tuple, Predicate pred) {
  size_t index = std::tuple_size<std::remove_reference_t<Tuple>>::value;
  size_t currentIndex = 0;
  bool found = false;

  for_each(tuple, [&](auto &&value) {
    if (!found && pred(value)) {
      index = currentIndex;
      found = true;
    }

    ++currentIndex;
  });

  return index;
}

template <typename Tuple, typename Action> void perform(Tuple &&tuple, size_t index, Action action) {
  size_t currentIndex = 0;
  for_each(tuple, [&action, index, &currentIndex](auto &&value) {
    if (currentIndex == index) {

      action(std::forward<decltype(value)>(value));
    }

    ++currentIndex;
  });
}

template <typename Tuple, typename Predicate> bool all_of(Tuple &&tuple, Predicate pred) {
  return find_if(tuple, std::not_fn(pred)) == std::tuple_size<std::decay_t<Tuple>>::value;
}

template <typename Tuple, typename Predicate> bool none_of(Tuple &&tuple, Predicate pred) { return find_if(tuple, pred) == std::tuple_size<std::decay_t<Tuple>>::value; }
template <typename Tuple, typename Predicate> bool any_of(Tuple &&tuple, Predicate pred) { return !none_of(tuple, pred); }

template <typename Tuple, typename Function> Tuple &operator|(Tuple &&tuple, Function func) {
  for_each(tuple, func);
  return tuple;
}

template <int trim, typename... T> auto subtuple(const std::tuple<T...> &t) { return detail::subtuple(t, std::make_index_sequence<sizeof...(T) - trim>()); }

template <size_t starting, size_t elems, class Tuple, class Seq = decltype(std::make_index_sequence<elems>())> struct sub_range;

template <size_t starting, size_t elems, class... Args, size_t... indx> struct sub_range<starting, elems, std::tuple<Args...>, std::index_sequence<indx...>> {
  static_assert(elems <= sizeof...(Args) - starting, "sub range is out of bounds!");
  using tuple = std::tuple<std::tuple_element_t<indx + starting, std::tuple<Args...>>...>;
};

template <typename Tuple, std::size_t... Ints> auto select_tuple(Tuple &&tuple, std::index_sequence<Ints...>) {
  return std::tuple<std::tuple_element_t<Ints, Tuple>...>(std::get<Ints>(std::forward<Tuple>(tuple))...);
}

template <class T, class Tuple> struct tuple_index;

template <class T, class... Types> struct tuple_index<T, std::tuple<T, Types...>> {
  static const std::size_t value = 0;
};

template <class T, class U, class... Types> struct tuple_index<T, std::tuple<U, Types...>> {
  static const std::size_t value = 1 + tuple_index<T, std::tuple<Types...>>::value;
};

// ZIP
template <typename Head, typename... Tail>
  requires((std::tuple_size_v<std::decay_t<Tail>> == std::tuple_size_v<std::decay_t<Head>>) && ...)
auto tuple_zip(Head &&head, Tail &&...tail) {
  return detail::tuple_zip_impl<Head, Tail...>(std::forward<Head>(head), std::forward<Tail>(tail)..., std::make_index_sequence<std::tuple_size_v<std::decay_t<Head>>>());
}
}; // namespace tp
