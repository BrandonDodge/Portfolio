# KU Final Language Project

## Overview

This project implements a small typed functional language in Haskell. The language includes the required base grammar from the final project prompt:

- integers
- booleans
- identifiers
- arithmetic: `+`, `-`, `*`, `/`, `^`
- `between`
- typed lambda expressions
- function application
- `bind`
- `if then else`
- boolean logic: `&&`, `||`
- comparison: `<=`
- `isZero`
- `Fix`
- types: `Num`, `Boolean`, and function types

The implementation also adds three new language features:

1. **Equality**
2. **Pairs**
3. **Sequencing with Unit**

The code is written as a direct abstract-syntax interpreter. A parser is not included because the project requirement is to define the language syntax, type inference, evaluation, and interpretation.

## File

- `FinalProject.hs` contains the full implementation and test suite.

## How to Run

```bash
runghc FinalProject.hs
```

or:

```bash
ghc FinalProject.hs
./FinalProject
```

## Main Design Choices

### Static Scoping

The evaluator uses closures. When a lambda is evaluated, it stores the current value environment inside `ClosureV`.

```haskell
eval (Lam x _ body) = do
  env <- ask
  return (ClosureV x body env)
```

When a function is applied, the body runs inside the closure's saved environment, extended with the new argument binding. That means free variables are resolved based on where the function was defined, not where it was called.

### Strict Call-by-Value

Function application evaluates the function expression and the argument expression before entering the function body.

```haskell
eval (App f a) = do
  fVal <- eval f
  aVal <- eval a
  applyValue fVal aVal
```

### Type Checking Before Evaluation

The interpreter first runs type inference. Evaluation only runs if a type is successfully inferred.

```haskell
interpret t = do
  inferredTy <- runR (typeof t) []
  value <- runR (eval t) []
  return (inferredTy, value)
```

This prevents obvious invalid programs like `true + 1` from reaching the evaluator.

### Bind

`bind` is implemented directly instead of elaborating it into lambda application. The reason is that the prompt's base grammar lists `bind id T T` without a type annotation. Direct implementation allows the type of the bound expression to be inferred first, then the body is checked under the extended context.

### Fix

`Fix` is implemented using a delayed recursive value:

```haskell
RecV String Term Env
```

This lets recursive functions work with a strict evaluator. The recursive value is only forced when the recursive identifier is actually used.

## New Feature 1: Equality

### Syntax

```haskell
Equal t1 t2
```

### Type Rule

Both terms must have the same comparable type. Comparable types are:

- `Num`
- `Boolean`
- `Unit`
- pairs whose components are comparable

Function values are intentionally not comparable.

### Example

```haskell
Equal (Plus (NumE 2) (NumE 3)) (NumE 5)
```

Result:

```haskell
Just (Boolean, true)
```

### Why It Matters

Equality is one of the most common operations in real languages. It is useful for decision-making, testing, and writing clearer programs.

## New Feature 2: Pairs

### Syntax

```haskell
PairE t1 t2
Fst t
Snd t
```

### Type Rule

If:

```haskell
t1 : A
t2 : B
```

then:

```haskell
PairE t1 t2 : A * B
```

For projections:

```haskell
Fst (A * B) : A
Snd (A * B) : B
```

### Example

```haskell
Fst (PairE (NumE 9) (BoolE False))
```

Result:

```haskell
Just (Num, 9)
```

### Why It Matters

Pairs let the language return and group multiple values without needing full structs, records, or objects.

## New Feature 3: Sequencing and Unit

### Syntax

```haskell
UnitE
Seq t1 t2
```

### Type Rule

`UnitE` has type `Unit`.

For sequencing, the first expression is checked and discarded. The result type is the type of the second expression.

```haskell
Seq t1 t2 : typeOf(t2)
```

### Example

```haskell
Seq UnitE (Plus (NumE 20) (NumE 22))
```

Result:

```haskell
Just (Num, 42)
```

### Why It Matters

Sequencing is useful because it gives the language a statement-like form. Even without mutable storage, it makes the language easier to extend later with side-effecting features.

## Tests Included

The project includes tests for:

1. Arithmetic
2. `between`
3. Static scoping with closures
4. Invalid type rejection before evaluation
5. Factorial using `Fix`
6. Curried function application
7. Equality
8. Pairs
9. Sequencing and Unit

Expected output should show all tests passing.

## Limitations

- This project does not include a text parser.
- Equality does not compare function closures.
- Division by zero returns `Nothing`.
- Negative exponents return `Nothing`.
- `Fix` can still diverge for non-terminating recursive definitions, which is normal for a language with recursion.
