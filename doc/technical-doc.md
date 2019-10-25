A short description of the internals of MRS 6
==================================================

MRS is an application targeted to information retrieval. The tasks it can provide are:

 * Fetch remote databanks using either built-in FTP or rsync
 * Unpack and partion a databank into separate documents
 * Parse documents to extract fields containing information
 * Create various kinds of indexes for these fields
 * A service to search these indexes, either based on full text search or more traditional database like index searches
 * A web based front end.
 * Formatting scripts in JavaScript to present the documents in more user friendly format
 * Links between documents within a databank and between databanks
 * A SOAP service for searching, blasting and aligning.

In addition to these main text retrieval based tasks, MRS can also store protein sequences and offers a Blast compatible search service.

This document will briefly describe the various tasks and will provide some rudimentary technical information.

Configuration
-------------

MRS is provided as a single application and a set of configuration files, parsers and formatting scripts and web template files. The main configuration file mrs-config.xml is used to specify what databanks will be presented to the end user and where the actual data is stored. This file is also used to specify what parser and formatter to use.

The config file has a DTD and the file will be validated against this DTD. MRS refuses to start if the config file contains validation errors.

Running MRS
-----------

To execute a task in MRS you start the application from the command line. When you enter only `mrs` you will see:

```
  Usage: mrs command [options]

  Command can be one of:

    version     Get MRS version
    blast       Do a blast search
    build       (Re-)build a databank
    dump        Dump index data
    entry       Retrieve and print an entry
    fetch       Fetch/mirror remote data for a databank
    info        Display information and statistics for a databank
    query       Perform a search in a databank
    server      Start or Stop a server session, or query the status
    vacuum      Clean up a databank reclaiming unused disk space
    validate    Perform a set of validation tests
    update      Same as build, but does a fetch first
    password    Generate password for use in configuration file

  Use mrs command --help for more info on each command
```

Each command has its own brief help page, e.g.

```
$ ./mrs build --help
mrs build:
  -d [ --databank ] arg Databank
  -c [ --config ] arg   Configuration file
  -v [ --verbose ]      Be verbose
  -a [ --threads ] arg  Nr of threads/pipelines
  -h [ --help ]         Display help message
```

The `dump` and `entry` commands can be used to retrieve the contents of an index or a single document respectively. Useful when debugging a parser e.g.

Automatic fetching and building can also be configured using the scheduler in the web application.

Fetching
--------

Databanks can be fetched using either ftp or rsync. The ftp client is built in, rsync will be executed in a separate process. The configuration file allows complex regular expressions to specify what should be downloaded. See the distributed file for samples.

Unpacking and Partitioning
--------------------------

When building a databank, MRS will first unpack the datafile or datafiles,
MRS has built-in decompressors for `.gz`, `.bz2`, '.Z' and `.tar` files. The resulting stream of data is fed to a number of Perl instances (you can specify how many using the `--threads` switch). Each Perl instance runs a copy of the associated parser script.

Parser scripts should contain an Perl object derived from `M6::Script` that have a method called `parse`. In the constructor of the parser script you can specify several fields that are used to partition a data stream into separate documents. These fields can contain either a string that should match exactly, or a regular expression.

| Field				| Function |
|-------------------|----------|
| header			| Everything matching this will be discarded. |
| lastheaderline	| Alternative for header, everything up to and including this will be discared  |
| trailer			| Stop processing data from here |
| firstdocline		| Indicates the start of a document |
| lastdocline		| Indicates the last line of a document |

For each databank a directory is created in the mrs-data/mrs directory. Documents created by the rules above are stored in a file called data in this databank directory. Documents are compressed using zlib.

Parsing
-------

After splitting a data stream into separate documents, these documents are fed to the `parse` method of the parser script. This method should identify the parts that need to be indexed. It also can set meta data for documents like `title` and `id`. This information is passed to MRS using the derived methods from `M6::Script`. The available methods are:

| Method              | Function |
|---------------------|----------|
| next_sequence_nr    | Ask for a unique number, useful to create ID's if the document does not provide it |
| set_attribute       | Set the content of a meta data field associated with this document |
| set_document        | Store something else as document instead of the default text as passed to `parse` |
| index_text          | Create a full text index and put this text in it, text will be tokenized |
| index_string        | Create a string index, the _untokenized_ text will be stored in the index |
| index_unique_string | Create a unique string index, the value passed should be unique for this document	|
| index_number	      | Create a numeric index |
| index_float		  | Create a floating point index |
| index_date		  | Crate a date index |
| add_link			  | Add a link to another document in possibly another databank |

A second method may be provided by the parser script called `to_fasta`. If this is available a blast index will be created.

### XML Parsing

A separate class of databanks are XML based. The configuration of these parsers is done in the `mrs-config.xml` file entirely, Perl is not used in this case. The parser specifies the document and fields using xpaths. Have a look at the config file for examples on how to use this.

Indexing
--------

During the build process all words (tokens) that are found are stored in a single lexicon in memory. The data structure used is based on a patricia tree and allows for two way access: the lexicon can very quickly return the unique id of a token and provide the stored token for an id.

For each document we store the triples consisting of document id, token id and index id. After parsing all documents we need to invert this list from a map of documents pointing to words to a map of words pointing to documents. Once this is done we can assemble and write out the indices.

As already shown in the parsing section, various index types can be created. The most important index is the full-text index. This contains all the words that were found by tokenizing text passed by `index_text`. As an extra, text passed to `index_string` will also be tokenized and words found will be stored in the full-text index as well.

The full-text index is also used to determine the ranking of search results. For each term the total frequency in the entire databank (`TF`) is stored as well as the frequence of the term in each document (`IDF`). Using the formula `TF * IDF` the weight of a document is calculated during retrieval and the results are sorted based on this weight.

When using `index_text` the location of the words inside a document is also stored. This enables searching for phrases, words that need be in consecutive order. This is also used to index and search logographs (chinese or japanese characters).

MRS contains two commands that can be used to check if a parser is producing the correct indices. The first is `mrs info` which prints out the information for a parsed databank. For SwissProt we get this e.g.

```
$ ./mrs info sprot
Statistics for databank "/srv/mrs-data/mrs/sprot.m6"

Version : 2019-10-25
Last Update : 2019-10-25 09:00 GMT

Number of documents :            561.176
Raw text in bytes   :      3.245.543.824
Data store size     :      1.664.204.800

Index Name           |                    | Nr of keys   | File size
-------------------------------------------------------------------------
de                   | word with position |      148.385 |      9.871.360
ft                   | word with position |    1.182.688 |     70.934.528
doi                  | string             |      215.459 |     11.935.744
ac                   | string             |      778.144 |     24.240.128
crc64                | string             |      473.271 |     19.578.880
pe                   | word with position |           15 |        917.504
cc                   | word with position |      480.360 |     44.982.272
os                   | word with position |       24.661 |      6.397.952
pubmed               | string             |      234.675 |      8.921.088
ref                  | word with position |      360.531 |     85.139.456
kw                   | string             |       72.045 |      7.127.040
dr                   | word with position |    4.754.329 |    232.439.808
dt                   | string             |          395 |      2.007.040
oh                   | word with position |        2.946 |        352.256
gn                   | word with position |      590.488 |     25.419.776
length               | number             |        3.368 |      1.179.648
mw                   | number             |      108.764 |      3.883.008
og                   | word with position |        1.157 |         98.304
ox                   | word with position |       15.878 |      1.671.168
oc                   | string             |        2.999 |      2.039.808
id                   | unique string      |      561.176 |      8.495.104
```

This gives an overview of what is stored. To see the actual keys in an index you can use the `mrs dump` command:

```
$ ./mrs dump -i doi sprot | head -10
10.$1107/s2059798317002029
10.1001/archderm.138.2.269
10.1001/archderm.138.4.501
10.1001/archderm.138.7.957
10.1001/archderm.138.9.1256
10.1001/archderm.139.4.498
10.1001/archderm.141.6.798
10.1001/archderm.1989.01670170047006
10.1001/archdermatol.2011.138
10.1001/archinte.161.20.2447
```

To emphasize the way indexing works, look at the doi index in the info above and you will see it is stored as a string. We can search for this key using the `mrs query` command:

```
$ ./mrs query -d sprot -q 'doi="10.1001/archdermatol.2011.138"'
SLUR1_HUMAN     Secreted Ly-6/uPAR-related protein 1
```

So, the doi `10.1001/archdermatol.2011.138` is found in only one document. We used the boolean syntax for searching the doi index here. But as mentioned, the content of string index keys are tokenized. This can be illustrated by this query:

```
$ ./mrs query -d sprot -q archdermatol
SLUR1_HUMAN     Secreted Ly-6/uPAR-related protein 1
```

Fortunately we get the exact same document for the word _archdermatol_ that was part of the doi. To check if this is really the right document, we retrieve it:

```
$ ./mrs entry -d sprot -e SLUR1_HUMAN
ID   SLUR1_HUMAN             Reviewed;         103 AA.
AC   P55000; Q53YJ6; Q6PUA6; Q92483;
...
RX   PubMed=21690549; DOI=10.1001/archdermatol.2011.138;
RA   Gruber R., Hennies H.C., Romani N., Schmuth M.;
RT   "A novel homozygous missense mutation in SLURP1 causing Mal de Meleda
RT   with an atypical phenotype.";
RL   Arch. Dermatol. 147:748-750(2011).
...
SQ   SEQUENCE   103 AA;  11186 MW;  07AAF6BCA8031282 CRC64;
     MASRWAVQLL LVAAWSMGCG EALKCYTCKE PMTSASCRTI TRCKPEDTAC MTTLVTVEAE
     YPFNQSPVVT RSCSSSCVAT DPDSIGAAHL IFCCFRDLCN SEL
//
```

And yes, the document contains the actual doi.

Searching
---------

Searching MRS will often be done using the web interface, but can be done using the `mrs query` command from the command line as well.

A query can consist of simple terms and/or boolean constructs. Terms are searched for using the full-text index. When this full-text index is used, the results will be sorted by weight as described above.

If the query contains boolean constructions, this boolean query will be used to filter results. If no regular search terms are given, the results of the boolean query will not be sorted.

If both search terms are available as well as a boolean query, the boolean query is used to filter the results of the full-text search.

To demonstrate this, we first search for `retinal degeneration slow`:

```
./mrs query -d sprot -q 'retinal degeneration slow'
PRPH2_CANLF     Peripherin-2
PRPH2_MOUSE     Peripherin-2
PRPH2_RAT       Peripherin-2
PRPH2_XENLA     Peripherin-2
PRPH2_HUMAN     Peripherin-2
PRPH2_FELCA     Peripherin-2
TM218_MOUSE     Transmembrane protein 218
PRPH2_CHICK     Peripherin-2
U119A_MOUSE     Protein unc-119 homolog A
OPSB_MOUSE      Short-wave-sensitive opsin 1
```

That was a full-text search containing three terms. We can search more specifically to the phrase `retinal degeneration slow` like this:

```
./mrs query -d sprot -q '"retinal degeneration slow"'
PRPH2_CANLF     Peripherin-2
PRPH2_MOUSE     Peripherin-2
PRPH2_RAT       Peripherin-2
PRPH2_XENLA     Peripherin-2
PRPH2_HUMAN     Peripherin-2
PRPH2_FELCA     Peripherin-2
PRPH2_CHICK     Peripherin-2
VAMP2_MOUSE     Vesicle-associated membrane protein 2
ROM1_MOUSE      Rod outer segment membrane protein 1
STX3_MOUSE      Syntaxin-3
```

As you'll notice, other results start showing up. Thats because our search terms are now placed in double quotes whichs tells mrs to add a boolean filter whereby the words should occur in exactly this order in the document.

Now if we want to limit this to only human entries we use the `os` (species) index. To do this prefix the terms with the index name and a colon:

```
$ ./mrs query -d sprot -q '"retinal degeneration slow" os:human'
PRPH2_HUMAN     Peripherin-2
```

The result is a single hit.

Note that we use a colon here, that means search for a term in a `string` index. If you use the equals sign you are search for the exact key in the index as was shown in the _doi_ example above.

Querying using the web interface works similar.

Web Application
---------------

MRS uses libzeep to generate a web front end. Libzeep has support for templating and expression language to generate web pages dynamically. For full documentation of libzeep look at [www.hekkelman.com](http://www.hekkelman.com/libzeep-doc/).

Formatting
----------

The rendering of pretty entries in a browser is done by either JavaScript based formatting scripts for regular text databanks or by xml stylesheets for XML based databanks.

Linking
-------

To quickly find related information, correct linking between documents is essential. Links are by nature bi-directional. That means, if you parse a document it may contain references to other documents and displaying these links is straightforward. But when display that other document you want to see the links to the document containing the reference too.

To achieve this, mrs creates index files for all links added with `add_link` in a subdirectory called `links` in the directory for a databank. When a documents' links are needed, the links from the originating databank are combined with those found in other databanks pointing to the current document.

SOAP service
------------

Again, libzeep is providing the necessary code to expose various features of MRS as a SOAP service. The WSDL's for these are generated on the fly.