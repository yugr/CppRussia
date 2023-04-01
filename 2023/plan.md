Одним из малоизвестных примеров Undefined Behavior в C/C++
является нарушение требований к функциям сравнения (компараторам).
Компараторы широко используются в алгоритмах (`std::sort`, `std::binary_search`, etc.)
и контейнерах (`std::set`, `std::map`) и встречаются как в C++,
так и в C (qsort, bsearch). Компараторы должны удовлетворять некоторым аксиомам,
которые в математике описываются понятием строгого слабого порядка
(strict weak ordering). Эти аксиомы неинтуитивны и в них легко ошибиться,
о чём свидетельствует большое количество соответствующих багов
в open-source проектах. Современные тулчейны предоставляют средства
для отслеживания таких ошибок и о них будет рассказано в докладе.

# Введение

Компаратор - функция-предикат для сравнения элементов какого-либо типа,
которая используется различными алгоритмами и контейнерами
стандартной библиотеки для упорядочения экземпляров этого типа.

Компаратор может быть указан явно или по умолчанию реализовываться
с помощью операторов `<` или (с C++20) `<=>`.

Контейнеры:
  * `std::map`, `std::multimap`
  * `std::set`, `std::multiset`

Алгоритмы:
  * `std::sort`, `std::stable_sort`
  * `std::binary_search`
  * `std::equal_range`, `std::lower_bound`, `std::upper_bound`
  * `std::min_element`, `std::max_element`
  * `std::nth_element`
  * и многие другие

# Пример ошибки

Рассмотрим простую программу:

```
#include <algorithm>
#include <vector>
#include <iostream>

#include <stdlib.h>

int main() {
  std::vector<int> vals;
  srand(0);
  for (size_t i = 0; i < N; ++i)
    vals.push_back((double)rand() / RAND_MAX * 10);
  std::sort(vals.begin(), vals.end(),
            [](int l, int r) { return l <= r; });
  for (auto v : vals) std::cout << v << "\n";
  return 0;
}
```

Кто сможет заметить в ней ошибку? (кроме того что плохо использовать `rand()` ;) )

Давайте посмотрим работает ли она:
```
$ g++ -g -DN=10 bad.cc && ./a.out
1
2
3
3
5
7
7
7
8
9
```

Вроде всё нормально. Но что если попробовать увеличить размер:
```
$ g++ -g -DN=50 bad.cc && ./a.out
1
4
1
1
9
2
5
8
0
0
273
0
0
0
1
1
1
1
1
1
2
2
2
2
2
2
3
3
3
3
4
4
4
4
5
5
5
5
5
6
6
6
6
6
7
7
7
7
7
7
double free or corruption (out)
Aborted
```

Как мы видим массив не отсортировался, да ещё и произошёл buffer overflow!
В чём же дело?

```
g++ -g -DN=50 -fsanitize=address -D_GLIBCXX_SANITIZE_VECTOR=1 bad.cc
./a.out
=================================================================
==143607==ERROR: AddressSanitizer: container-overflow on address 0x611000000108 at pc 0x55fa93254d5d bp 0x7fff1ed30c80 sp 0x7fff1ed30c78
READ of size 4 at 0x611000000108 thread T0
    #0 0x55fa93254d5c in operator()<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, __gnu_cxx::__normal_iterator<int*, std::vector<int> > > /usr/include/c++/10/bits/predefined_ops.h:156
    #1 0x55fa93255164 in __unguarded_partition<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, __gnu_cxx::__ops::_Iter_comp_iter<main()::<lambda(int, int)> > > /usr/include/c++/10/bits/stl_algo.h:1904
    #2 0x55fa9325428b in __unguarded_partition_pivot<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, __gnu_cxx::__ops::_Iter_comp_iter<main()::<lambda(int, int)> > > /usr/include/c++/10/bits/stl_algo.h:1926
    #3 0x55fa93253d1f in __introsort_loop<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, long int, __gnu_cxx::__ops::_Iter_comp_iter<main()::<lambda(int, int)> > > /usr/include/c++/10/bits/stl_algo.h:1958
    #4 0x55fa93253d3d in __introsort_loop<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, long int, __gnu_cxx::__ops::_Iter_comp_iter<main()::<lambda(int, int)> > > /usr/include/c++/10/bits/stl_algo.h:1959
    #5 0x55fa93253d3d in __introsort_loop<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, long int, __gnu_cxx::__ops::_Iter_comp_iter<main()::<lambda(int, int)> > > /usr/include/c++/10/bits/stl_algo.h:1959
    #6 0x55fa93253a6f in __sort<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, __gnu_cxx::__ops::_Iter_comp_iter<main()::<lambda(int, int)> > > /usr/include/c++/10/bits/stl_algo.h:1974
    #7 0x55fa932537fb in sort<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, main()::<lambda(int, int)> > /usr/include/c++/10/bits/stl_algo.h:4894
    #8 0x55fa932534ca in main /home/yugr/tasks/CppRussia/bad.cc:11
    #9 0x7f9bba05ad09 in __libc_start_main ../csu/libc-start.c:308
    #10 0x55fa93253249 in _start (/home/yugr/tasks/CppRussia/a.out+0x2249)

0x611000000108 is located 200 bytes inside of 256-byte region [0x611000000040,0x611000000140)
allocated by thread T0 here:
    #0 0x7f9bba49e647 in operator new(unsigned long) ../../../../src/libsanitizer/asan/asan_new_delete.cpp:99
    #1 0x55fa93258f68 in __gnu_cxx::new_allocator<int>::allocate(unsigned long, void const*) /usr/include/c++/10/ext/new_allocator.h:115
    #2 0x55fa932587c9 in std::allocator_traits<std::allocator<int> >::allocate(std::allocator<int>&, unsigned long) /usr/include/c++/10/bits/alloc_traits.h:460
    #3 0x55fa9325834f in std::_Vector_base<int, std::allocator<int> >::_M_allocate(unsigned long) /usr/include/c++/10/bits/stl_vector.h:346
    #4 0x55fa93257a07 in void std::vector<int, std::allocator<int> >::_M_realloc_insert<int>(__gnu_cxx::__normal_iterator<int*, std::vector<int, std::allocator<int> > >, int&&) /usr/include/c++/10/bits/vector.tcc:440
    #5 0x55fa9325745e in void std::vector<int, std::allocator<int> >::emplace_back<int>(int&&) /usr/include/c++/10/bits/vector.tcc:121
    #6 0x55fa93256dbd in std::vector<int, std::allocator<int> >::push_back(int&&) /usr/include/c++/10/bits/stl_vector.h:1204
    #7 0x55fa93253483 in main /home/yugr/tasks/CppRussia/bad.cc:10
    #8 0x7f9bba05ad09 in __libc_start_main ../csu/libc-start.c:308

HINT: if you don't care about these errors you may set ASAN_OPTIONS=detect_container_overflow=0.
If you suspect a false positive see also: https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow.
SUMMARY: AddressSanitizer: container-overflow /usr/include/c++/10/bits/predefined_ops.h:156 in operator()<__gnu_cxx::__normal_iterator<int*, std::vector<int> >, __gnu_cxx::__normal_iterator<int*, std::vector<int> > >
Shadow bytes around the buggy address:
  0x0c227fff7fd0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x0c227fff7fe0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x0c227fff7ff0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x0c227fff8000: fa fa fa fa fa fa fa fa 00 00 00 00 00 00 00 00
  0x0c227fff8010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
=>0x0c227fff8020: 00[fc]fc fc fc fc fc fc fa fa fa fa fa fa fa fa
  0x0c227fff8030: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x0c227fff8040: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x0c227fff8050: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x0c227fff8060: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x0c227fff8070: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
  Shadow gap:              cc
==143607==ABORTING
```

Итак мы видим что неправильно написанные компараторы могут приводить к серьезным проблемам.

# Причина ошибки

Давайте заглянем под капот `std::sort` и попробуем разобраться что же пошло не так.

Упавший код выполняет основной шаг быстрой сортировки -
разбиение массива по опорному элементу `__pivot`
(код намеренно упрощён):
```
_RandomAccessIterator
__unguarded_partition(_RandomAccessIterator __first,
                      _RandomAccessIterator __last,
                      T __pivot,
                      _Compare __comp)
{
  while (true)
    {
      while (__comp(*__first, __pivot))
        ++__first;
      --__last;
      while (__comp(__pivot, *__last))
        --__last;
      if (!(__first < __last))
        return __first;
      std::iter_swap(__first, __last);
      ++__first;
    }
}
```

Переполнение буфера происходит в первом while-цикле.
Как мы видим здесь используется неявное предположение о том,
что найдётся элемент, не меньший `__pivot`.

Оно обусловлено алгоритмом выбора `__pivot`: он выбирается
как медиана трёх элементов массива (первого, среднего и последнего):
```
inline _RandomAccessIterator
__unguarded_partition_pivot(_RandomAccessIterator __first,
                            _RandomAccessIterator __last, _Compare __comp)
{
  _RandomAccessIterator __mid = __first + (__last - __first) / 2;
  std::__move_median_to_first(__first, __first + 1, __mid, __last - 1,
                              __comp);
  return std::__unguarded_partition(__first + 1, __last, __first, __comp);
}
```

Т.о. образом при входе в цикл всегда существуют элементы массива, т.ч.
```
__comp(a, __pivot) && __comp(__pivot, b)
```
(это упрощение т.к. вообще говоря опорный элемент может совпадать с граничными,
но ограничимся им для простоты формул).

Из этого по идее должно следовать условие что
```
!(__pivot < __a) && !(__b < __pivot)
```
которое и гарантировало бы что циклы while не выйдут за границы массива.

Но особенность нашего компаратора заключается как раз в том, что замена
```
__comp(a, __pivot) -> !__comp(__pivot, __a)
```
неправомерна и приводит к тому, что нужный инвариант цикла перестаёт выполняться
и происходит переполнение буфера.

Для того чтобы таких ошибок не происходило,
компараторы должны удовлетворять набору специальных правил,
называемых аксиомами.

Эти аксиомы не специфичны для C++ и встречаются также в других языках,
например [C](https://pubs.opengroup.org/onlinepubs/009696899/functions/qsort.html),
[Java](https://docs.oracle.com/javase/8/docs/api/java/lang/Comparable.html) и
[Lua](https://stackoverflow.com/a/49625819/2170527).

По сути они задают универсальные требования, при которых можно непротиворечиво упорядочить элементы типа.

# Аксиомы компараторов: частичный порядок

Аксиомы, которым должны подчиняться компараторы, изложены в стандарте языка,
и в том числе на Cppref: https://en.cppreference.com/w/cpp/named_req/Compare

Грубо говоря компаратор должен вести себя как "нормальный" `operator <`.

Во-первых компаратор должен удовлетворять трём логичным аксиомам:

1) comp(a, a) == false (иррефлексивность)
2) comp(a, b) == true -> comp(b, a) == false (антисимметричность)
3) comp(a, b) == true && comp(b, c) == true -> comp(a, c) == true (транзитивность)

На самом деле можно обойтись только двумя аксиомами (например из 2 следует 1),
но для ясности обычно выписывают все три.

Такие компараторы называются строгими частичными порядками и знакомы многим из курса общей алгебры
(Частично Упорядоченные Множества, Partially Ordered Sets).

Без выполнения этих аксиом понятие сортировки попросту теряет смысл.
Например невозможно отсортировать элементы в игре камень-ножницы-бумага,
т.к. они не удовлетворяют аксиоме транзитивности.

# Аксиомы компараторов: несравнимость

Для того чтобы сформулировать последнюю аксиому нам надо сначала заметить
что с каждым компаратором связанa ещё одна функция ("отношение" в терминах алгебры):
```
bool equiv(T a, T b) { return comp(a, b) == false && comp(b, a) == false; }
```
Её называют отношением несравнимости (incomparability) или эквивалентности.

Это отношение определяет что два элемента типа "неразличимы" с точки зрения компаратора `comp`.
Оно похоже на некоторое "равенство" или эквивалентность элементов типа друг другу.
Вообще говоря оно никак не связано с обычным равенством, задаваемым `operator ==` класса.

Так вот, отношение `equiv` должно удовлетворять четвёртой аксиоме:

4) equiv(a, b) == true && equiv(b, c) == true -> equiv(a,c) == true

(транзитивность несравнимости).

Грубо говоря эта аксиома гарантирует нам что сортируемое множество
можно разбить на группы "равных" элементов (называемых классами эквивалентности)
и эти группы будут вести себя одинаково в сравнениях:
сравнение любого экземпляра группы с другими элементами множества будет давать
одинаковый результат независимо от выбора экземпляра.

# Strict weak ordering

Алгоритмы сортировки полагаются на все 4 приведённые аксиомы. Некоторые из них могут быть выведены из остальных.

Все вместе они называются термином "строгий слабый порядок" (strict weak ordering)

Из https://timsong-cpp.github.io/cppwp/n4868/alg.sorting :
```
For algorithms other than those described in [alg.binary.search], comp shall induce a strict weak ordering on the values.
```

Из https://timsong-cpp.github.io/cppwp/n4868/utility.arg.requirements#:Cpp17LessThanComparable :
```
< is a strict weak ordering relation
```

# Spaceship-оператор и другие виды порядков в C++20

Стандарт языка двигается в сторону явного представления понятия порядка в языке.

В C++20 введён новый тип оператора сравнения - оператор трехзначного сравнения
(three-way comparison aka spaceship operator, `<=>`).

Его основное назначение состоит в сокращении объёма кода:
вместо 6 операторов сравнения (`<`, `<=`, `>`, `>=`, `==`, `!=`)
теперь достаточно реализовать один оператор `<=>`.

Интересной особенностью spaceship-оператора является то,
что он может возвращать значение одного из трёх типов (comparison categories),
в зависимости от того какой вид порядка реализует класс:
1) `std::partial_ordering`
2) `std::weak_ordering`
3) `std::strong_ordering`

Теоретически выбор того или иного возвращаемого значения должен
гарантировать что для объектов класса выполняются соответствующие аксиомы:
1) partial ordering: иррефлексивность, антисимметричность, транзитивность
2) weak ordering: то же + транзитивность несравнимости
3) strong ordering: то же + возможность подстановки "равных" элементов

К сожалению на данный момент эти требования явно не прописаны в стандарте
и выбор того или иного типа возвращаемого значения
не накладывает дополнительных требований на поведение класса
и скорее служит для целей документирования
(подробности см. в дискуссии на stackoverflow:
https://stackoverflow.com/questions/75770367/implied-meaning-of-ordering-types-in-c20).

# Примеры ошибок со stackoverflow и OSS проектов

1) неправильная реализация лексикографического порядка

Это пожалуй самый распространённая ошибка при написании компараторов.

Переходить к сравнению поля `second` можно только если элементы `first` равны:
```
bool operator<(const SomeClass &rhs) const {
  if (first < rhs.first)
    return true;
  else if (second < rhs.second) // ЗАБЫЛИ "&& fist == rhs.first"
    return true;
  else
    return false;
}
```

В данном случае будет нарушена аксиома асимметричности:
```
{100, 2} < {200, 1}
```
но при этом таже
```
{200, 1} < {100, 2}
```

Ошибку легко исправить, добавив сравнение:
```
bool operator<(const SomeClass &rhs) const {
  if (first < rhs.first)
    return true;
  else if (first == rhs.first) {
    if (second < rhs.second)
      return true;
    else
      return false;
  }
}
```
но более правильные решения:
[O
  * использовать `std::tie` и встроенный оператор сравнения кортежей:
```
[](SomeClass l, SomeClass r) {
  return std::tie(l.first, l.second) < std::tie(r.first, r.second);
}
```
  * с C++20 - использовать реализацию `operator <=>` по умолчанию:
```
auto operator <=>(const SomeClass &) const = default;
```

Примеры со stackoverflow:
  * https://stackoverflow.com/questions/48455244/bug-in-stdsort
  * https://stackoverflow.com/questions/53712873/sorting-a-vector-of-a-custom-class-with-stdsort-causes-a-segmentation-fault
  * https://stackoverflow.com/questions/68225770/sorting-vector-of-pair-using-lambda-predicate-crashing-with-memory-corruption
  * https://stackoverflow.com/questions/72737018/stdsort-results-in-a-segfault

2) Использование нестрогого порядка вместо строгого

Пример этой ошибки мы уже видели вначале, при ней нарушаются первые две аксиомы.

Ещё примеры со stackoverflow.com:
  * https://stackoverflow.com/questions/40483971/program-crash-in-stdsort-sometimes-cant-reproduce
  * https://stackoverflow.com/questions/65468629/stl-sort-debug-assertion-failed
  * https://stackoverflow.com/questions/18291620/why-will-stdsort-crash-if-the-comparison-function-is-not-as-operator
  * https://stackoverflow.com/questions/19757210/stdsort-from-algorithm-crashes
  * https://stackoverflow.com/questions/64014782/c-program-crashes-when-trying-to-sort-a-vector-of-strings
  * https://stackoverflow.com/questions/70869803/c-code-crashes-when-trying-to-sort-2d-vector
  * https://stackoverflow.com/questions/67553073/std-sort-sometimes-throws-seqmention-fault

3) Отрицание строгого порядка не является строгим порядком

Другая вариация той же ошибки:
```
auto lt = std::less<int>();
auto inv_lt = std::not2(lt);

std::sort(..., ..., inv_lt);
```

В данном случае программист рассчитывал на то что отрицание корректного компаратора
также является компаратором.

Это не так: отрицанием строгого порядка является нестрогий порядок,
который как мы видели в начале презентации приводит к ошибкам.

Пример из жизни: https://schneide.blog/2010/11/01/bug-hunting-fun-with-stdsort/

4) массивы содержащие NaN

Типы с плавающей точкой поддерживают специальные значения NaN,
которые возникают в результате некорректных вычислений
(например извлечения корня из отрицательного числа или деления `0/0`).

Сравнение с NaN всегда возвращает false, поэтому NaN рассматривается
как эквивалентный остальным числам.

Это приводит к нарушению транзитивности эквивалентности (4 аксиома):
`1.0 ~ NAN` и `NAN ~ 2.0`, но не `1.0 ~ 2.0`.

На практике это приводит к неправильной сортировке массивов содержащих NaNs:

```
#include <algorithm>
#include <iostream>
#include <math.h>

int main() {
  double a[] = {
    100,
    5,
    3,
    NAN,
    200,
    11
  };
  std::sort(&a[0], &a[sizeof(a)/sizeof(a[0])]);
  for (auto x : a) std::cout << x << "\n";
  return 0;
}
```

```
$ g++ bad.cc && ./a.out
3
5
100
nan
11
200
```

Исправить эту программу несложно - достаточно перед сортировкой
избавиться от NaN'ов с помощью std::partition:
```
auto end = std::partition(&a[0], &a[sizeof(a)/sizeof(a[0])],
                            [](double x) { return !isnan(x); });
std::sort(&a[0], end);
```

Такой код отработает корректно:
```
3
5
11
100
200
nan
```

Пример из жизни: https://stackoverflow.com/questions/9244243/strict-weak-ordering-and-stdsort

5) некорректная обработка специального случая

```
[](std::uniq_ptr<SomeClass> l, std::uniq_ptr<SomeClass> r) {
  if (!l.get())
    return true;
  else
    ...
}
```

В данном случае нарушаются требования иррефлексивности и антисимметричности
в случае когда второй операнд также нулевой.

Достаточно корректно обработать этот случай:
```
if (!l.get())
  return r.get();
```

Примеры со stackoverflow:
  * https://stackoverflow.com/questions/55815423/stdsort-crashes-with-strict-weak-ordering-comparing-with-garbage-values
  * https://stackoverflow.com/questions/48972158/crash-in-stdsort-sorting-without-strict-weak-ordering

6) сравнение особых объектов отдельным алгоритмом

```
[](Object l, Object r) {
  if (is_special(l) && is_special(r))
    return comp_special(l, r);
  else
    return comp_normal(l, r);
}
```

Очень легко нарушить условие транзитивности.

Например если
```
comp_special(special_obj1, special_obj2) && comp_normal(special_obj2, normal_obj)
```
то должно быть
```
comp_normal(special_obj1, normal_obj)
```
но это часто не выполняется т.к `comp_special` и `comp_normal` как правило
логически (и алгоритмически) никак не связаны между собой
(обычно они сравнивают совершенно разные поля объектов).

Пример из жизни: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68988

7) приближенные сравнения

Часто встречаются компараторы, в которых делается сравнение "с точность до эпсилон":
```
bool cmp(double a, double b) {
    if (abs(a - b) < eps) return false;
    return a < b;
}
```

Программист хотел чтобы "близкие" элементы рассматривались как эквивалентные,
но при это нарушил аксиому транзитивности эквивалентности:
```
equiv(0, 0.5 * eps) == true && equiv(0.5 * eps, eps) == true && cmp(0, eps) == false
```

# Отладочные средства в тулчейнах

В libstdc++ с помощью макроса `-D_GLIBCXX_DEBUG` можно включить дополнительную проверку
иррефлексивности:
```
$ cat /usr/include/c++/10/bits/stl_algo.h
...
inline void
sort(_RandomAccessIterator __first, _RandomAccessIterator __last,
     _Compare __comp)
{
  ...
  __glibcxx_requires_irreflexive_pred(__first, __last, __comp);

  std::__sort(__first, __last, __gnu_cxx::__ops::__iter_comp_iter(__comp));
}
...
```
(она бы нашла ошибку из начала презентации).

В libc++ [проверяется асимметричность](https://libcxx.llvm.org/DesignDocs/DebugMode.html)
```
Libc++ provides some checks for the consistency of comparators passed to algorithms. Specifically, many algorithms such as binary_search, merge, next_permutation, and sort, wrap the user-provided comparator to assert that !comp(y, x) whenever comp(x, y). This can cause the user-provided comparator to be evaluated up to twice as many times as it would be without the debug mode, and causes the library to violate some of the Standard’s complexity clauses.
```
Чтобы включить её нужно указать при компиляции `-D_LIBCPP_ENABLE_DEBUG_MODE`.

Обе опции имеют существенный (2x) оверхед, поэтому их рекомендуется использовать только для тестирования.

Эти чекеры не меняют алгоритмическую сложность алгоритма (`O(N*logN)`),
поэтому не могут провести полную проверку корректности
(и в частности проверку условия транзитивности).

# SortChecker

Два простых динамических чекера для проверки корректности в рантайме:

  * SortChecker (https://github.com/yugr/sortcheck)
    - работает с программами на C
    - перехватывает и проверяет API типа `qsort` и `bsearch`
    - основан на динамической инструментации (`LD_PRELOAD`)
    - 15 ошибок в различных OSS проектах (GCC, Harfbuzz, etc.)
  * SortChecker++ (https://github.com/yugr/sortcheckxx)
    - работает с программами на C++
    - перехватывает и проверяет API типа `std::sort`
    - основан на source-to-source инструментации (Clang-based)
    - 5 ошибок в различных OSS проектах
    - TODO: поддержка всех релевантных алгоритмов (`nth_element`, etc.)

# Как использовать

Вначале инструментируем код:
```
sortcheckxx/bin/SortChecker tmp.cc -- -DN=50
```

По большому счёта вся инструментация сводится к замене
вызова стандартной `std::sort`
```
std::sort(vals.begin(), vals.end(),
          [](int l, int r) { return l <= r; });
```
на отладочную обёртку
```
sortcheck::sort_checked(vals.begin(), vals.end(),
          [](int l, int r) { return l <= r; }, __FILE__, __LINE__);
```

Скомпилировав и запустив инструментированный код
```
g++ -g -DN=50 -I sortcheckxx/include tmp.cc
./a.out
```
получаем
```
sortcheck: tmp.cc:14: irreflexive comparator at position 0
Aborted
```

# Псевдокод

Каждый запуск `std::sort` и аналогичных API предваряется проверками:

```
for x in array
  if comp(x, x)
    error

for x, y in array
  if comp(x, y) !=  comp(y, x)
    error

for x, y, z in array
  if comp(x, y) && comp(y, x) && !comp(x, z)
    error

for x, y, z in array
  if equiv(x, y) && equiv(y, x) && !equiv(x, z)
    error
```

Можно видеть что сложность проверок составляет `O(N^3)`, что существенно превосходит даже `std::sort`,
не говоря о более быстрых алгоритмах (`std::max_element`, etc.).

Поэтому на практике обходится не весь массив, а его небольшое подмножество (20-30 элементов).

# Быстрый алгоритм проверки

* https://github.com/danlark1/quadratic_strict_weak_ordering
* Предложен Д. Кутениным в начале 2023 года
* Идея алгоритма:
  * Предварительно отсортировать массив устойчивым алгоритмом
  * Выделять в отсортированном массиве префиксы эквивалентных элементов
  * И проверять их на транзитивность с оставшейся частью массива
* Снижает сложность до `O(N^2)` (по прежнему превосходит сложность большинства алгоритмов)

# Что почитать

* Danila Kutenin [Changing std::sort at Google’s Scale and Beyond](https://danlark.org/2022/04/20/changing-stdsort-at-googles-scale-and-beyond/)
* Jonathan Müller [Mathematics behind Comparison](https://www.foonathan.net/2018/06/equivalence-relations/)

# Домашнее задание

1) Изучите типичные ошибки и не повторяйте их в работе
2) Включите `GLIBCXX_DEBUG` и `_LIBCPP_ENABLE_DEBUG_MODE` в своём CI
3) Натравите `Sortchecker` (и `Sortchecker++`) на свой код
    * сообщения об ошибках и дополнения приветствуются!

# Другие типы ошибок в компараторных API

1) Неотсортированные массивы в API типа `bsearch`
  * поддерживается в SortChecker/SortChecker++
2) Неопределённый порядок сортировки эквивалентных элементов
  * проверяется в libc++ с помощью рандомизации `-D_LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY_SEED'

# Заключение

Результаты получены на системе:
```
$ uname -a
Linux gcc13 5.10.0-20-amd64 #1 SMP Debian 5.10.158-2 (2022-12-13) x86_64 GNU/Linux
```

* Ссылки на профили
* Попросить делиться идеями чекеров
