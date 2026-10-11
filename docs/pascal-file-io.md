# Apollo Pascal and Gemini Pick Runtime Integration

## Overview

Apollo Pascal will implement a standard Pascal language environment, including conventional Pascal file handling facilities where supported by the target runtime.

When targeting the Gemini Pick System, standard Pascal file operations will not be available. Instead, programs will interact with the Pick database through a dedicated Pick runtime library that exposes Pick-native concepts and operations.

This design preserves both:

- Standards-compliant Pascal semantics.
- Authentic Pick database behaviour.

## Design Rationale

Historically, Pick systems were not based around conventional hierarchical filesystems. Application developers worked directly with:

- Files
- Item IDs
- Records
- Attributes
- Values
- Select lists

The primary programming interface was the Pick database itself rather than stream-oriented file I/O.

Although languages such as COBOL, Pascal and C were available on some Pick implementations, database access was generally provided through runtime libraries rather than native language file abstractions.

Therefore, Apollo should expose Pick functionality explicitly rather than attempting to emulate standard Pascal files on top of Pick records.

## Runtime-Based Approach

Apollo remains a Pascal compiler regardless of target platform.

The availability of file handling facilities is determined by the selected Gemini runtime personality.

### Gemini Native Runtime

Supports standard Pascal file operations.

Examples:

```pascal
var
  F : Text;
  Customers : file of Customer;
```

Supported operations:

```pascal
Reset
Rewrite
Read
ReadLn
Write
WriteLn
BlockRead
BlockWrite
```

### Gemini Pick Runtime

Standard Pascal files are unavailable.

Instead, the runtime provides Pick-specific types and functions.

Examples:

```pascal
var
  Customers : PickFile;
  Rec : String;
```

Supported operations:

```pascal
PickOpen
PickRead
PickWrite
PickDelete
PickSelect
PickReadNext
```

## Why Standard Pascal Files Are Disabled

Pick records do not map naturally onto Pascal file structures because:

- Records are variable length.
- Data is organised as attributes, values and subvalues.
- Item IDs form part of the access model.
- Dictionaries define interpretation of data.
- Select lists are a core database concept.
- Pick files are not stream-oriented.

Attempting to treat Pick files as:

```pascal
file of RecordType
```

would introduce unnecessary complexity and obscure the underlying Pick model.

## Preferred Programming Model

Instead of:

```pascal
Assign(F, 'CUSTOMERS');
Reset(F);
```

Pick applications should use explicit database operations:

```pascal
PickOpen('CUSTOMERS', Customers);

if PickRead(Customers, '100123', Rec) then
begin
  WriteLn(Rec);
end;
```

This makes it immediately clear that the application is interacting with a MultiValue database rather than a conventional filesystem.

## Proposed Runtime API

### File Access

```pascal
function PickOpen(
  const FileName : String;
  var Handle : PickFile
) : Boolean;

function PickRead(
  Handle : PickFile;
  const ItemId : String;
  var RecordData : String
) : Boolean;

function PickWrite(
  Handle : PickFile;
  const ItemId : String;
  const RecordData : String
) : Boolean;

function PickDelete(
  Handle : PickFile;
  const ItemId : String
) : Boolean;
```

### Select Processing

```pascal
function PickSelect(
  Handle : PickFile;
  var List : PickSelectList
) : Boolean;

function PickReadNext(
  List : PickSelectList;
  var ItemId : String
) : Boolean;
```

### Record Manipulation

```pascal
function GetAttr(
  const RecordData : String;
  AttrNo : Integer
) : String;

procedure SetAttr(
  var RecordData : String;
  AttrNo : Integer;
  const Value : String
);

function GetValue(
  const RecordData : String;
  AttrNo : Integer;
  ValueNo : Integer
) : String;
```

These operations provide Pascal equivalents of common Pick BASIC operations without exposing attribute mark delimiters directly.

## Compiler Behaviour

Apollo should perform compile-time validation against the target runtime.

For example:

```pascal
var
  F : Text;
```

should compile successfully when targeting:

```text
GEMINI_NATIVE
```

but produce a compilation error when targeting:

```text
GEMINI_PICK
```

Example diagnostic:

```text
TEXT files are unavailable in target runtime GEMINI_PICK.
Use PickFile and Pick database routines instead.
```

Compile-time validation is preferred over runtime failure because it makes platform restrictions explicit and predictable.

## Internal VM Architecture

The Gemini VM should expose a common record-management interface that can be used by multiple language runtimes.

Example internal operations:

```text
DB.OPEN
DB.READ
DB.WRITE
DB.DELETE
DB.SELECT
DB.READNEXT
DB.LOCK
DB.RELEASE
```

Language-specific runtimes become thin wrappers over these VM services.

### Pick BASIC

```basic
READ REC FROM F,ID
```

### Apollo Pascal

```pascal
PickRead(F, ID, Rec)
```

### Future COBOL

```cobol
READ CUSTOMER-FILE RECORD
```

All ultimately invoke the same underlying VM database operations.

## Benefits

- Preserves standard Pascal behaviour.
- Preserves authentic Pick database concepts.
- Avoids artificial file abstractions.
- Simplifies compiler implementation.
- Supports future language integration.
- Aligns with historical Pick development practices.
- Fits naturally into Gemini's plugin-based filesystem architecture.

## Conclusion

Apollo Pascal should be implemented as a standards-oriented Pascal compiler whose available runtime features are determined by the selected Gemini target.

When targeting Gemini Pick:

- Standard Pascal file handling is not available.
- Pick-specific database APIs are provided instead.
- Applications work directly with Pick concepts such as files, item IDs, records and select lists.

This approach accurately reflects how Pascal and other non-BASIC languages historically interacted with Pick systems while maintaining a clean and extensible Gemini architecture.