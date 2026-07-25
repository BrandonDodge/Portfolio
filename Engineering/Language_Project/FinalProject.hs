{-# OPTIONS_GHC -Wall #-}

-- KU Final Language Project
-- A small typed functional language with:
--   * numbers, booleans, arithmetic, comparison, conditionals
--   * typed lambdas, application, bind, and fixed-point recursion
--   * strict call-by-value evaluation
--   * static scoping through closures
--   * type inference before evaluation
--   * three added features: equality, pairs, and sequencing/unit
--
-- Run with:
--   runghc FinalProject.hs
--
-- This file intentionally works directly with abstract syntax trees. A parser is
-- not required by the project prompt because the assignment asks for type
-- inference, evaluation, and interpretation over the language syntax.

module Main where

--------------------------------------------------------------------------------
-- A small Reader-with-failure computation
--------------------------------------------------------------------------------

newtype Reader env a = Reader { runR :: env -> Maybe a }

instance Functor (Reader env) where
  fmap f (Reader g) = Reader (\env -> fmap f (g env))

instance Applicative (Reader env) where
  pure x = Reader (\_ -> Just x)
  (Reader rf) <*> (Reader rx) =
    Reader
      ( \env -> do
          f <- rf env
          x <- rx env
          return (f x)
      )

instance Monad (Reader env) where
  (Reader rx) >>= f =
    Reader
      ( \env -> do
          x <- rx env
          runR (f x) env
      )

ask :: Reader env env
ask = Reader Just

local :: (env -> env) -> Reader env a -> Reader env a
local f (Reader r) = Reader (\env -> r (f env))

abortR :: Reader env a
abortR = Reader (\_ -> Nothing)

liftMaybe :: Maybe a -> Reader env a
liftMaybe mx = Reader (\_ -> mx)

--------------------------------------------------------------------------------
-- Type language
--------------------------------------------------------------------------------

data Ty
  = TNum
  | TBool
  | TUnit
  | TPair Ty Ty
  | Ty :-> Ty
  deriving (Eq)

infixr 5 :->

instance Show Ty where
  show TNum = "Num"
  show TBool = "Boolean"
  show TUnit = "Unit"
  show (TPair a b) = "(" ++ show a ++ " * " ++ show b ++ ")"
  show (a :-> b) = showLeft a ++ " -> " ++ show b
    where
      showLeft t@(_ :-> _) = "(" ++ show t ++ ")"
      showLeft t = show t

--------------------------------------------------------------------------------
-- Abstract syntax
--------------------------------------------------------------------------------

data Term
  = NumE Int
  | BoolE Bool
  | Id String
  | Plus Term Term
  | Minus Term Term
  | Times Term Term
  | DivE Term Term
  | Pow Term Term
  | Between Term Term Term
  | Lam String Ty Term
  | App Term Term
  | Bind String Term Term
  | If Term Term Term
  | And Term Term
  | Or Term Term
  | LE Term Term
  | IsZero Term
  | Fix Term

  -- New feature 1: Unit and sequencing.
  | UnitE
  | Seq Term Term

  -- New feature 2: Equality for comparable values.
  | Equal Term Term

  -- New feature 3: Pairs and projections.
  | PairE Term Term
  | Fst Term
  | Snd Term
  deriving (Eq, Show)

--------------------------------------------------------------------------------
-- Runtime values
--------------------------------------------------------------------------------

data Value
  = NumV Int
  | BoolV Bool
  | UnitV
  | PairV Value Value
  | ClosureV String Term Env
  | RecV String Term Env

type Env = [(String, Value)]
type Context = [(String, Ty)]

instance Show Value where
  show (NumV n) = show n
  show (BoolV b) = if b then "true" else "false"
  show UnitV = "unit"
  show (PairV a b) = "(" ++ show a ++ ", " ++ show b ++ ")"
  show (ClosureV x _ _) = "<closure " ++ x ++ ">"
  show (RecV x _ _) = "<recursive " ++ x ++ ">"

valueEq :: Value -> Value -> Bool
valueEq (NumV a) (NumV b) = a == b
valueEq (BoolV a) (BoolV b) = a == b
valueEq UnitV UnitV = True
valueEq (PairV a1 b1) (PairV a2 b2) = valueEq a1 a2 && valueEq b1 b2
valueEq _ _ = False

--------------------------------------------------------------------------------
-- Type inference
--------------------------------------------------------------------------------

typeof :: Term -> Reader Context Ty
typeof (NumE _) = return TNum
typeof (BoolE _) = return TBool
typeof (Id x) = do
  ctx <- ask
  liftMaybe (lookup x ctx)

typeof (Plus a b) = numericBinary a b TNum
typeof (Minus a b) = numericBinary a b TNum
typeof (Times a b) = numericBinary a b TNum
typeof (DivE a b) = numericBinary a b TNum
typeof (Pow a b) = numericBinary a b TNum

typeof (Between x lo hi) = do
  tx <- typeof x
  tlo <- typeof lo
  thi <- typeof hi
  if tx == TNum && tlo == TNum && thi == TNum
    then return TBool
    else abortR

typeof (Lam x domain body) = do
  range <- local ((x, domain) :) (typeof body)
  return (domain :-> range)

typeof (App f a) = do
  fTy <- typeof f
  aTy <- typeof a
  case fTy of
    domain :-> range
      | domain == aTy -> return range
    _ -> abortR

-- Bind is implemented directly instead of being elaborated into a lambda.
-- That keeps the external syntax close to the prompt's grammar:
--     bind id T T
-- The type of the bound expression is inferred first, then the body is checked
-- under the extended type context.
typeof (Bind x bound body) = do
  boundTy <- typeof bound
  local ((x, boundTy) :) (typeof body)

typeof (If c t e) = do
  cTy <- typeof c
  tTy <- typeof t
  eTy <- typeof e
  if cTy == TBool && tTy == eTy
    then return tTy
    else abortR

typeof (And a b) = boolBinary a b TBool
typeof (Or a b) = boolBinary a b TBool
typeof (LE a b) = numericBinary a b TBool

typeof (IsZero t) = do
  tTy <- typeof t
  if tTy == TNum then return TBool else abortR

typeof (Fix t) = do
  tTy <- typeof t
  case tTy of
    domain :-> range
      | domain == range -> return range
    _ -> abortR

-- New feature 1: Unit and sequencing.
typeof UnitE = return TUnit
typeof (Seq first second) = do
  _ <- typeof first
  typeof second

-- New feature 2: Equality.
typeof (Equal a b) = do
  aTy <- typeof a
  bTy <- typeof b
  if aTy == bTy && comparable aTy
    then return TBool
    else abortR

-- New feature 3: Pairs.
typeof (PairE a b) = do
  aTy <- typeof a
  bTy <- typeof b
  return (TPair aTy bTy)

typeof (Fst p) = do
  pTy <- typeof p
  case pTy of
    TPair left _ -> return left
    _ -> abortR

typeof (Snd p) = do
  pTy <- typeof p
  case pTy of
    TPair _ right -> return right
    _ -> abortR

numericBinary :: Term -> Term -> Ty -> Reader Context Ty
numericBinary a b resultTy = do
  aTy <- typeof a
  bTy <- typeof b
  if aTy == TNum && bTy == TNum
    then return resultTy
    else abortR

boolBinary :: Term -> Term -> Ty -> Reader Context Ty
boolBinary a b resultTy = do
  aTy <- typeof a
  bTy <- typeof b
  if aTy == TBool && bTy == TBool
    then return resultTy
    else abortR

comparable :: Ty -> Bool
comparable TNum = True
comparable TBool = True
comparable TUnit = True
comparable (TPair a b) = comparable a && comparable b
comparable (_ :-> _) = False

--------------------------------------------------------------------------------
-- Evaluation
--------------------------------------------------------------------------------

eval :: Term -> Reader Env Value
eval (NumE n) = return (NumV n)
eval (BoolE b) = return (BoolV b)

eval (Id x) = do
  env <- ask
  case lookup x env of
    Just v -> liftMaybe (forceValue v)
    Nothing -> abortR

eval (Plus a b) = numericEval (+) a b
eval (Minus a b) = numericEval (-) a b
eval (Times a b) = numericEval (*) a b

eval (DivE a b) = do
  va <- eval a
  vb <- eval b
  case (va, vb) of
    (NumV _, NumV 0) -> abortR
    (NumV x, NumV y) -> return (NumV (x `div` y))
    _ -> abortR

eval (Pow a b) = do
  va <- eval a
  vb <- eval b
  case (va, vb) of
    (NumV x, NumV y)
      | y >= 0 -> return (NumV (x ^ y))
    _ -> abortR

-- between x lo hi means lo <= x <= hi
eval (Between x lo hi) = do
  vx <- eval x
  vlo <- eval lo
  vhi <- eval hi
  case (vx, vlo, vhi) of
    (NumV n, NumV low, NumV high) -> return (BoolV (low <= n && n <= high))
    _ -> abortR

eval (Lam x _ body) = do
  env <- ask
  return (ClosureV x body env)

eval (App f a) = do
  fVal <- eval f
  aVal <- eval a
  applyValue fVal aVal

eval (Bind x bound body) = do
  v <- eval bound
  local ((x, v) :) (eval body)

eval (If c t e) = do
  cVal <- eval c
  case cVal of
    BoolV True -> eval t
    BoolV False -> eval e
    _ -> abortR

eval (And a b) = do
  va <- eval a
  vb <- eval b
  case (va, vb) of
    (BoolV x, BoolV y) -> return (BoolV (x && y))
    _ -> abortR

eval (Or a b) = do
  va <- eval a
  vb <- eval b
  case (va, vb) of
    (BoolV x, BoolV y) -> return (BoolV (x || y))
    _ -> abortR

eval (LE a b) = do
  va <- eval a
  vb <- eval b
  case (va, vb) of
    (NumV x, NumV y) -> return (BoolV (x <= y))
    _ -> abortR

eval (IsZero t) = do
  v <- eval t
  case v of
    NumV n -> return (BoolV (n == 0))
    _ -> abortR

eval (Fix t) = do
  v <- eval t
  case forceValue v of
    Just (ClosureV x body closureEnv) ->
      local (const ((x, RecV x body closureEnv) : closureEnv)) (eval body)
    _ -> abortR

-- New feature 1: Unit and sequencing.
eval UnitE = return UnitV
eval (Seq first second) = do
  _ <- eval first
  eval second

-- New feature 2: Equality.
eval (Equal a b) = do
  va <- eval a
  vb <- eval b
  case eqValue va vb of
    Just result -> return (BoolV result)
    Nothing -> abortR

-- New feature 3: Pairs.
eval (PairE a b) = do
  va <- eval a
  vb <- eval b
  return (PairV va vb)

eval (Fst p) = do
  v <- eval p
  case v of
    PairV left _ -> return left
    _ -> abortR

eval (Snd p) = do
  v <- eval p
  case v of
    PairV _ right -> return right
    _ -> abortR

numericEval :: (Int -> Int -> Int) -> Term -> Term -> Reader Env Value
numericEval op a b = do
  va <- eval a
  vb <- eval b
  case (va, vb) of
    (NumV x, NumV y) -> return (NumV (op x y))
    _ -> abortR

applyValue :: Value -> Value -> Reader Env Value
applyValue v arg =
  case forceValue v of
    Just (ClosureV x body closureEnv) ->
      local (const ((x, arg) : closureEnv)) (eval body)
    _ -> abortR

-- RecV is a small delayed fixed point. It is only forced when the recursive
-- identifier is actually used. This avoids the strict evaluator immediately
-- expanding recursion before the function body has a chance to receive an input.
forceValue :: Value -> Maybe Value
forceValue (RecV x body closureEnv) =
  runR (eval body) ((x, RecV x body closureEnv) : closureEnv)
forceValue v = Just v

eqValue :: Value -> Value -> Maybe Bool
eqValue (NumV a) (NumV b) = Just (a == b)
eqValue (BoolV a) (BoolV b) = Just (a == b)
eqValue UnitV UnitV = Just True
eqValue (PairV a1 b1) (PairV a2 b2) = do
  left <- eqValue a1 a2
  right <- eqValue b1 b2
  return (left && right)
eqValue _ _ = Nothing

--------------------------------------------------------------------------------
-- Interpreter
--------------------------------------------------------------------------------

interpret :: Term -> Maybe (Ty, Value)
interpret t = do
  inferredTy <- runR (typeof t) []
  value <- runR (eval t) []
  return (inferredTy, value)

--------------------------------------------------------------------------------
-- Example programs and tests
--------------------------------------------------------------------------------

staticScopingExample :: Term
staticScopingExample =
  Bind "n" (NumE 1)
    ( Bind "f" (Lam "x" TNum (Plus (Id "x") (Id "n")))
        ( Bind "n" (NumE 2)
            (App (Id "f") (NumE 1))
        )
    )

factBuilder :: Term
factBuilder =
  Lam "rec" (TNum :-> TNum)
    ( Lam "n" TNum
        ( If (IsZero (Id "n"))
            (NumE 1)
            (Times (Id "n") (App (Id "rec") (Minus (Id "n") (NumE 1))))
        )
    )

factorialFive :: Term
factorialFive = App (Fix factBuilder) (NumE 5)

curriedAdd :: Term
curriedAdd =
  Lam "x" TNum
    (Lam "y" TNum (Plus (Id "x") (Id "y")))

tests :: [(String, Term, Maybe (Ty, Value))]
tests =
  [ ( "arithmetic"
    , Plus (NumE 10) (Times (NumE 3) (NumE 2))
    , Just (TNum, NumV 16)
    )
  , ( "between"
    , Between (NumE 5) (NumE 1) (NumE 10)
    , Just (TBool, BoolV True)
    )
  , ( "static scoping with closures"
    , staticScopingExample
    , Just (TNum, NumV 2)
    )
  , ( "bad type is rejected before eval"
    , Plus (BoolE True) (NumE 1)
    , Nothing
    )
  , ( "factorial through Fix"
    , factorialFive
    , Just (TNum, NumV 120)
    )
  , ( "curried function application"
    , App (App curriedAdd (NumE 7)) (NumE 8)
    , Just (TNum, NumV 15)
    )
  , ( "new equality feature"
    , Equal (Plus (NumE 2) (NumE 3)) (NumE 5)
    , Just (TBool, BoolV True)
    )
  , ( "new pair feature"
    , Fst (PairE (NumE 9) (BoolE False))
    , Just (TNum, NumV 9)
    )
  , ( "new sequencing/unit feature"
    , Seq UnitE (Plus (NumE 20) (NumE 22))
    , Just (TNum, NumV 42)
    )
  ]

sameResult :: Maybe (Ty, Value) -> Maybe (Ty, Value) -> Bool
sameResult Nothing Nothing = True
sameResult (Just (ty1, v1)) (Just (ty2, v2)) = ty1 == ty2 && valueEq v1 v2
sameResult _ _ = False

showResult :: Maybe (Ty, Value) -> String
showResult Nothing = "Nothing"
showResult (Just (ty, val)) = "Just (" ++ show ty ++ ", " ++ show val ++ ")"

runTest :: (String, Term, Maybe (Ty, Value)) -> IO ()
runTest (name, program, expected) = do
  let actual = interpret program
      status = if sameResult actual expected then "PASS" else "FAIL"
  putStrLn ("[" ++ status ++ "] " ++ name)
  putStrLn ("  expected: " ++ showResult expected)
  putStrLn ("  actual:   " ++ showResult actual)
  putStrLn ""

main :: IO ()
main = mapM_ runTest tests
