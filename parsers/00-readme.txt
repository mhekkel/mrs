Parser documentation for MRS version 6

1. Identify documents

Flat text databanks contain documents in some structured way. Sometimes it is
possible to locate a new document by looking at the first line, in other cases
documents are separated by a special string. And then databanks can have
leading or trailing pieces of text that should be ignored.

A parser script specifies the way to divide a text file into documents by
setting properties in the constructor for the parser. Recognized properties
are:

	header, lastheaderline, trailer, firstdocline and lastdocline
	
For each of these properties you can assign a simple string in which case the
line should match exactly, or you can assign a regular expression and then
this expression is used to match a line.

If lastheaderline is defined, header is ignored.

If you did not provide any property, MRS assumes one document per file.

2. Parsing documents

The sub method of a parser is called with the document as second parameter.
It is the task of the parser to identify indexable text and pass it to MRS.

For this there are the functions:

  index_text
	Uses a tokenizer to split the text into words and numbers and stores
	each along with their position in the document.
	
  index_unique_string
	Stores the passed string verbatim in an index. The value should be 
	unique, used for e.g. ID fields.
	
  index_string
	Stores the passed string verbatim, but it does not have to be unique.
  
  index_number
	Stores the number in an index that is sorted on numeric value rather
	than the textual representation.

The parser can append meta data to a document using the set_attribute call.
Two attributes are strongly recommended, one is 'id' and the other is 'title'.
They are not strictly needed, but are very useful.

If links between the current document and another document can be found, the
link information can be stored using the add_link call. The first parameter
is the databank containing the linked document, the second is the ID of this
document. The databank provided may also be an alias for one or more
databanks.

And then the parser can override the content of the document by calling
set_document. See for an example the oxford parser.

If the documents do not contain unique ID fields, you can use the
next_sequence_nr call to generate unique numbers for a newly created databank
that can then be used to create IDs.

3. FastA extraction

If the parser is used for a databank that should have a FastaA file, it should
contain a to_fasta method. This method will receive four useful parameters
to use:

  text
	The entire document
  db
	The databank ID or mnemonic (e.g. 'sprot' for SwissProt)
  id
	The content of the id attribute, or the document number.
  title
	The title stored as attribute

The to_fasta function should return a FastA format containing an ID for each
sequence that looks like this:

	>gnl|$db|$id|seqnr $title

The seqnr field is optional, it is only needed when multiple sequences can be
found in a single document (PDB e.g.).

This to_fasta function is called during indexing and can be called by the
web application.
