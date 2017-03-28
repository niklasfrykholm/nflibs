# nflibs

*nflibs* is a collection of  C99 utility libraries. Each library has a singular specific purpose and a minimalistic decoupled API. But unlike for example the  great `stb_*` libraries, they are not completely isolated units. Instead, they have been written to work together. For example, *nf_config_data* uses *nf_string_table* to store strings and *nf_json_parser* produces output in the *nf_config_data* format.

## License

*nflibs* is in the public domain.

## Libraries

This section provides a short description of all the different libraries, see the `*.c` files for a detailed description of each library.

### nf_config_data

Manipulates *configuration data* objects. This is intended as a general way of representing arbitrary data and corresponds to data that can be stored in a JSON file (bools, numbers, strings, arrays and objects). The data is kept in a single memory block that can be saved to and loaded from disk without any need for pointer patching.

### nf_json_parser

Parses JSON files into *Config Data* objects. Can also parse JSON extensions, such as comments, python multline strings, etc, based on configuration settings.

### nf_memory_tracker

Tracks memory allocations in a buffer that can be streamed out to disk or network for later analysis.

### nf_string_table

Implements string to symbol (integer) conversion (i.e. interning), for more compact representation of string data.

## Design Philosophy

The *nflibs* are an experiment in creating a set of libraries that interoperate (so that higher level functionality can be built on top of a lower level foundation), but still are decoupled and isolated enough that you can plug them into any kind of project without feeling too bad about it.

### C99

The libraries are written in C99 rather than C++. There are a number of reasons for this:

* Since C++ is so huge and everybody uses a different subset of it, making a reusable library that fits well into different C++ projects is difficult.
* C99 has ABI compatibility, meaning that with FFI the libraries can be used from a number of different scripting languages.
* C++ is a complex monster that nobody should be forced to learn.

If you are using Visual Studio, you need 2013 to compile the libraries,
because that's when Microsoft decided to add (partial) C99 support. Feel free to pester Microsoft about adding support for the `inline` keyword.

### Single file interface

The libraries are designed with a *single file* design philosophy. Each library is fully stored in a single `.c` file that contains the interface, the implementation and any unit tests. Basically, the idea is that you should be able to completely understand a library by looking at a single file. Also, that it should be easy to pick and choose just the libraries that you need.

The most controversial choice is to not use any *header files*. Instead, each `.c` file includes the definitions for the external structs and functions that it needs to use. This is a deliberate choice to encourage a decoupled design and shorter compile times. But it is sort of an experiment, I might decide to change it later if it becomes to unwieldy.

If you want header files you can create them with:

    make header

This creates a single merged `nflibs.h` header file.

Unit tests are stored directly in the `.c` files. If you compile a library with `-DNFXX_UNIT_TEST` (where *XX* is the abbreviation for that library) it will create an executable that runs the unit tests. A successful test run produces no output.

To compile and run all unit tests, use:

    make run_all_tests

This is also the default target, so you can type just `make` to do the same thing.

### Memory allocations

All libraries are written in a data-oriented style. They try to minimize memory allocations and lay the data out linearly in memory for improved cache performance.

Most of the libraries allocate the data in a single memory block. The block starts with a header which is followed by bulk data. References within the memory block use offsets rather than allocators, so it can be moved freely in memory and reallocated with `realloc`.

Typically the same data structure is used for both online and offline (disk) storage, so the data can be read directly from disk into memory and then be dynamically modified. Since dynamic data has other demands the data may be laid out in a slightly different (but still compatible) way. Typically, in these cases, a `pack()` function is offered that packs the data into the most efficient format for static use.

The libraries never use `malloc()` or `free()` directly. Instead the user can pass an allocator function. The allocator function always use the format:

```cpp
typedef void *(*realloc_f)(void *ptr, int osize, int nsize, char *file, int line, char *tag);
```

Basically this is `realloc()`, but with some additional information (file name, line number, tag), that can be used for memory tracking.

Note that the old size of the allocation is always passed in the `osize` parameter, because the libraries always keep track of the size of their allocations. You can use this to build a more efficient allocator, since you don't need to track the size of the memory blocks.

### `int` vs `unsigned`

`nflibs` use `int` everywhere except where it is explicitly manipulating bit flags. The extra bit you get from using `unsigned` is usually not worth the extra risk of unintentional underflow.

For 32 bit values, `int` and `unsigned` are used to avoid having to include `stdint` everywhere. For other bit sizes, the bit size is always explicitly specified: `int16_t`, `uint64_t`, etc.

### Documentation

Functions are documented in the implementation rather than in the interface in order to keep the interface absolutely minimal. (Both to minimize compile times and to make it easy to get a quick overview of the interface.)

The documentation uses Markdown syntax. You can extract the documentation into Markdown files by running:

    make doc
