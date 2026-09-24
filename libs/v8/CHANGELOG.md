# V8 Library for AROS - Changelog

## [Unreleased] - 2025-12-09

### Fixed - V8 Monolith Linking

- **V8 Engine Integration**: Fixed linking against libv8_monolith.a
  - Changed from `USER_LDFLAGS` with `-lv8_monolith` to `USER_OBJS` with direct path
  - Resolves issue where v8.library was only 800KB instead of including the ~74-75MB V8 engine
  - AROS modules built with `$(AROS_LD) -Ur` don't properly resolve `-l` flags for static libraries
  - Using `USER_OBJS` ensures the full static library is passed directly to the linker
  - Pattern confirmed by arch/all-pc/bootstrap/mmakefile.src

### Documentation
- Updated BUILD_NOTES.md with V8 Engine Integration section
- Added Issue #5 documenting the linking problem and fix
- Corrected C++ standard version (C++17 → C++20)
- Made file size references consistent (approximately 74-75MB)

## [Unreleased] - 2025-12-05

### Added - Array Support

- **Array Literals**: Support for `[1, 2, 3]` syntax
  - Empty arrays: `[]`
  - Arrays with expressions: `[1 + 1, 2 * 2]`
  - Nested arrays: `[[1, 2], [3, 4]]`
  - Arrays stored as string representation (e.g., `[1,2,3]`)

- **Array Indexing**: Support for `arr[index]` syntax
  - Zero-based indexing: `[10, 20, 30][0]` returns `10`
  - Immediate literal indexing: `[1, 2, 3][1]` returns `2`
  - Variable array indexing: `arr[0]` after `arr = [1, 2, 3]`
  - Out-of-bounds access returns `undefined`

- **Array `.length` Property**: Access array length
  - `[1, 2, 3].length` returns `3`
  - `[].length` returns `0`
  - Variable access: `arr.length` after setting `arr`

- **String `.length` Property**: Access string length
  - `s.length` returns the length of string stored in `s`

### Changed
- Added TOKEN_LBRACKET, TOKEN_RBRACKET, TOKEN_DOT token types
- Updated tokenizer to recognize `[`, `]`, and `.` tokens
- Added is_array_value(), get_array_length(), get_array_element() helpers
- Enhanced evaluate_primary() for array literal and indexing support
- Updated expression terminator list to include TOKEN_RBRACKET

### Documentation
- Updated V8_PORTING_ROADMAP.md capabilities list
- Added test_v8_arrays.c in developer/debug/test/v8/
- Updated mmakefile.src to build new test program

### Test Coverage
New test program (test_v8_arrays.c) validates:
- Empty and non-empty array literals
- Array element access at various indices
- Out-of-bounds access returning undefined
- Nested array support
- Array .length property
- String .length property
- Arrays in arithmetic expressions
- Array element comparisons

---

## [Previous Changes] - 2025-12-05

### Added - Extended Operator Support

- **Unsigned Right Shift Operator (`>>>`)**: Zero-fill right shift
  - Converts operand to unsigned 32-bit integer
  - Returns unsigned result (e.g., `-1 >>> 0` returns `4294967295`)
  - Properly handles negative numbers

- **Comma Operator (`,`)**: Expression sequencing
  - Evaluates expressions from left to right
  - Returns the value of the last expression
  - Useful for multiple assignments: `x = 1, y = 2, x + y`

- **Increment/Decrement Operators (`++`, `--`)**:
  - Prefix increment (`++x`): Increments and returns new value
  - Prefix decrement (`--x`): Decrements and returns new value
  - Postfix increment (`x++`): Returns old value, then increments
  - Postfix decrement (`x--`): Returns old value, then decrements
  - Proper side effects on stored properties

- **Simple Assignment Operator (`=`)**: Variable assignment
  - Assigns value to property and returns the value
  - Works with the property storage system

- **Compound Assignment Operators**:
  - Arithmetic: `+=`, `-=`, `*=`, `/=`, `%=`
  - Bitwise: `&=`, `|=`, `^=`
  - Shift: `<<=`, `>>=`, `>>>=`
  - String concatenation with `+=`

### Changed
- Updated tokenizer to recognize new operators
- Added TOKEN_COMMA, TOKEN_INCREMENT, TOKEN_DECREMENT types
- Updated operator precedence table with assignment operators
- Enhanced evaluate_expression_internal() for compound assignments

### Documentation
- Updated V8_PORTING_ROADMAP.md with new operator support
- Added test_v8_extended.c in developer/debug/test/v8/
- Updated mmakefile.src to build new test program

### Test Coverage
New test program (test_v8_extended.c) validates:
- Unsigned right shift with positive and negative values
- Comma operator with multiple expressions
- All prefix and postfix increment/decrement operations
- Side effect verification for ++/--
- All compound assignment operators
- Bitwise and shift compound assignments

## [1.1.0] - 2025-01-13

### Added - Enhanced Simple Evaluator
- **Comparison Operators**: Support for `==`, `!=`, `<`, `>`, `<=`, `>=`
  - Numeric comparisons with proper type conversion
  - String comparisons using lexicographic order
  - Returns boolean results ("true" or "false")

- **Logical Operators**: Support for `&&`, `||`, `!`
  - AND operator (`&&`): Returns true if both operands are truthy
  - OR operator (`||`): Returns true if at least one operand is truthy
  - NOT operator (`!`): Negates truthy/falsy value
  - Proper JavaScript truthy/falsy semantics

- **Boolean Literals**: Support for `true` and `false`
  - Can be used directly in expressions
  - Properly evaluated in logical operations
  - Stored and retrieved through property system

- **Special Values**: Support for `null` and `undefined`
  - Can be used as literal values
  - Treated as falsy in logical operations
  - Stored and retrieved through property system

- **Enhanced Tokenizer**:
  - Multi-character operator recognition (==, !=, <=, >=, &&, ||)
  - Keyword detection (true, false, null, undefined)
  - Token types for booleans, null, undefined
  - Parenthesis tokens (for future use)

- **Helper Functions**:
  - `compare_values()`: Compares two values with specified operator
  - `is_truthy()`: Determines if a value is truthy per JavaScript rules

### Changed
- Enhanced `V8_EvaluateSimpleExpression()` to support new operators
- Updated token type enumeration with new types
- Improved property reference resolution for boolean values

### Documentation
- Added comprehensive test program (`test_v8_advanced.c`) with 20 test cases
- Updated README.md with new feature examples
- Updated IMPLEMENTATION_BREADCRUMBS.md with enhanced status
- Added this CHANGELOG.md to track changes

### Test Coverage
New test program validates:
- Boolean literal evaluation (true, false)
- Null and undefined literals
- All comparison operators with numeric and string values
- All logical operators (&&, ||, !)
- Property integration with boolean values
- Mixed operations with properties and literals

## [1.0.0] - Initial Framework Release

### Added
- Complete AROS library structure with LVO table
- 14 public API functions for JavaScript execution
- Internal memory management using exec.library
- Thread-safe isolate and context management
- Basic expression evaluator supporting:
  - Arithmetic operations: +, -, *, /
  - String concatenation
  - Property storage and retrieval
  - Property references in expressions
- Comprehensive error handling
- Full documentation suite:
  - README.md - User guide
  - INTEGRATION_GUIDE.md - Application integration
  - PORTING_GUIDE.md - V8 porting instructions
  - IMPLEMENTATION_BREADCRUMBS.md - Status tracking
  - PROJECT_SUMMARY.md - Project overview
- Test programs:
  - test_v8.c - Basic functionality tests
  - test_v8_enhanced.c - Enhanced evaluator tests
- Build system integration with AROS mmake

### Notes
- This is a complete framework awaiting actual V8 engine integration
- Simple evaluator provides basic JavaScript functionality as temporary solution
- All stub implementations have TODO comments for V8 integration
