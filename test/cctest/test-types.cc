// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "src/hydrogen-types.h"
#include "src/types.h"
#include "test/cctest/cctest.h"
#include "test/cctest/types-fuzz.h"

using namespace v8::internal;


// Testing auxiliaries (breaking the Type abstraction).


static bool IsInteger(double x) {
  return nearbyint(x) == x && !i::IsMinusZero(x);  // Allows for infinities.
}


static bool IsInteger(i::Object* x) {
  return x->IsNumber() && IsInteger(x->Number());
}


typedef uint32_t bitset;


struct ZoneRep {
  typedef void* Struct;

  static bool IsStruct(Type* t, int tag) {
    return !IsBitset(t) && reinterpret_cast<intptr_t>(AsStruct(t)[0]) == tag;
  }
  static bool IsBitset(Type* t) { return reinterpret_cast<uintptr_t>(t) & 1; }
  // HACK: the number 5 below is the value of StructuralType::kUnionTag.
  static bool IsUnion(Type* t) { return t->IsUnionForTesting(); }

  static Struct* AsStruct(Type* t) {
    return reinterpret_cast<Struct*>(t);
  }
  static bitset AsBitset(Type* t) {
#if defined(V8_PPC_TAGGING_OPT)
    return static_cast<bitset>(reinterpret_cast<uintptr_t>(t) >> 1);
#else
    return static_cast<bitset>(reinterpret_cast<uintptr_t>(t) ^ 1u);
#endif
  }
  static Struct* AsUnion(Type* t) {
    return AsStruct(t);
  }
  static int Length(Struct* structured) {
    return static_cast<int>(reinterpret_cast<intptr_t>(structured[1]));
  }

  static Zone* ToRegion(Zone* zone, Isolate* isolate) { return zone; }

  struct BitsetType : Type::BitsetType {
    using Type::BitsetType::New;
    using Type::BitsetType::Glb;
    using Type::BitsetType::Lub;
    using Type::BitsetType::IsInhabited;
  };
};


struct HeapRep {
  typedef FixedArray Struct;

  static bool IsStruct(Handle<HeapType> t, int tag) {
    return t->IsFixedArray() && Smi::cast(AsStruct(t)->get(0))->value() == tag;
  }
  static bool IsBitset(Handle<HeapType> t) { return t->IsSmi(); }
  // HACK: the number 5 below is the value of StructuralType::kUnionTag.
  static bool IsUnion(Handle<HeapType> t) { return t->IsUnionForTesting(); }

  static Struct* AsStruct(Handle<HeapType> t) { return FixedArray::cast(*t); }
  static bitset AsBitset(Handle<HeapType> t) {
#if defined(V8_PPC_TAGGING_OPT)
    return static_cast<bitset>(
      reinterpret_cast<uintptr_t>(*t) >> (kSmiTagSize + kSmiShiftSize));
#else
    return static_cast<bitset>(reinterpret_cast<uintptr_t>(*t));
#endif
  }
  static Struct* AsUnion(Handle<HeapType> t) { return AsStruct(t); }
  static int Length(Struct* structured) { return structured->length() - 1; }

  static Isolate* ToRegion(Zone* zone, Isolate* isolate) { return isolate; }

  struct BitsetType : HeapType::BitsetType {
    using HeapType::BitsetType::New;
    using HeapType::BitsetType::Glb;
    using HeapType::BitsetType::Lub;
    using HeapType::BitsetType::IsInhabited;
    static bitset Glb(Handle<HeapType> type) { return Glb(*type); }
    static bitset Lub(Handle<HeapType> type) { return Lub(*type); }
  };
};


template<class Type, class TypeHandle, class Region, class Rep>
struct Tests : Rep {
  typedef Types<Type, TypeHandle, Region> TypesInstance;
  typedef typename TypesInstance::TypeVector::iterator TypeIterator;
  typedef typename TypesInstance::MapVector::iterator MapIterator;
  typedef typename TypesInstance::ValueVector::iterator ValueIterator;

  Isolate* isolate;
  HandleScope scope;
  Zone zone;
  TypesInstance T;

  Tests()
      : isolate(CcTest::InitIsolateOnce()),
        scope(isolate),
        zone(),
        T(Rep::ToRegion(&zone, isolate), isolate,
          isolate->random_number_generator()) {}

  bool Equal(TypeHandle type1, TypeHandle type2) {
    return
        type1->Equals(type2) &&
        this->IsBitset(type1) == this->IsBitset(type2) &&
        this->IsUnion(type1) == this->IsUnion(type2) &&
        type1->NumClasses() == type2->NumClasses() &&
        type1->NumConstants() == type2->NumConstants() &&
        (!this->IsBitset(type1) ||
          this->AsBitset(type1) == this->AsBitset(type2)) &&
        (!this->IsUnion(type1) ||
          this->Length(this->AsUnion(type1)) ==
              this->Length(this->AsUnion(type2)));
  }

  void CheckEqual(TypeHandle type1, TypeHandle type2) {
    CHECK(Equal(type1, type2));
  }

  void CheckSub(TypeHandle type1, TypeHandle type2) {
    CHECK(type1->Is(type2));
    CHECK(!type2->Is(type1));
    if (this->IsBitset(type1) && this->IsBitset(type2)) {
      CHECK(this->AsBitset(type1) != this->AsBitset(type2));
    }
  }

  void CheckSubOrEqual(TypeHandle type1, TypeHandle type2) {
    CHECK(type1->Is(type2));
    if (this->IsBitset(type1) && this->IsBitset(type2)) {
      CHECK((this->AsBitset(type1) | this->AsBitset(type2))
            == this->AsBitset(type2));
    }
  }

  void CheckUnordered(TypeHandle type1, TypeHandle type2) {
    CHECK(!type1->Is(type2));
    CHECK(!type2->Is(type1));
    if (this->IsBitset(type1) && this->IsBitset(type2)) {
      CHECK(this->AsBitset(type1) != this->AsBitset(type2));
    }
  }

  void CheckOverlap(TypeHandle type1, TypeHandle type2) {
    CHECK(type1->Maybe(type2));
    CHECK(type2->Maybe(type1));
  }

  void CheckDisjoint(TypeHandle type1, TypeHandle type2) {
    CHECK(!type1->Is(type2));
    CHECK(!type2->Is(type1));
    CHECK(!type1->Maybe(type2));
    CHECK(!type2->Maybe(type1));
  }

  void IsSomeType() {
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle t = *it;
      CHECK(1 ==
          this->IsBitset(t) + t->IsClass() + t->IsConstant() + t->IsRange() +
          this->IsUnion(t) + t->IsArray() + t->IsFunction() + t->IsContext());
    }
  }

  void Bitset() {
    // None and Any are bitsets.
    CHECK(this->IsBitset(T.None));
    CHECK(this->IsBitset(T.Any));

    CHECK(bitset(0) == this->AsBitset(T.None));
#if defined(V8_PPC_TAGGING_OPT)
    CHECK(bitset(0x7fffffffu) == this->AsBitset(T.Any));
#else
    CHECK(bitset(0xfffffffeu) == this->AsBitset(T.Any));
#endif

    // Union(T1, T2) is bitset for bitsets T1,T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle union12 = T.Union(type1, type2);
        CHECK(!(this->IsBitset(type1) && this->IsBitset(type2)) ||
              this->IsBitset(union12));
      }
    }

    // Intersect(T1, T2) is bitset for bitsets T1,T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle intersect12 = T.Intersect(type1, type2);
        CHECK(!(this->IsBitset(type1) && this->IsBitset(type2)) ||
              this->IsBitset(intersect12));
      }
    }

    // Union(T1, T2) is bitset if T2 is bitset and T1->Is(T2)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle union12 = T.Union(type1, type2);
        CHECK(!(this->IsBitset(type2) && type1->Is(type2)) ||
              this->IsBitset(union12));
      }
    }

    // Union(T1, T2) is bitwise disjunction for bitsets T1,T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle union12 = T.Union(type1, type2);
        if (this->IsBitset(type1) && this->IsBitset(type2)) {
          CHECK(
              (this->AsBitset(type1) | this->AsBitset(type2)) ==
              this->AsBitset(union12));
        }
      }
    }

    // Intersect(T1, T2) is bitwise conjunction for bitsets T1,T2 (modulo None)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        if (this->IsBitset(type1) && this->IsBitset(type2)) {
          TypeHandle intersect12 = T.Intersect(type1, type2);
          bitset bits = this->AsBitset(type1) & this->AsBitset(type2);
          CHECK(bits == this->AsBitset(intersect12));
        }
      }
    }
  }

  void PointwiseRepresentation() {
    // Check we can decompose type into semantics and representation and
    // then compose it back to get an equivalent type.
    int counter = 0;
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      counter++;
      printf("Counter: %i\n", counter);
      fflush(stdout);
      TypeHandle type1 = *it1;
      TypeHandle representation = T.Representation(type1);
      TypeHandle semantic = T.Semantic(type1);
      TypeHandle composed = T.Union(representation, semantic);
      CHECK(type1->Equals(composed));
    }

    // Pointwiseness of Union.
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle representation1 = T.Representation(type1);
        TypeHandle semantic1 = T.Semantic(type1);
        TypeHandle representation2 = T.Representation(type2);
        TypeHandle semantic2 = T.Semantic(type2);
        TypeHandle direct_union = T.Union(type1, type2);
        TypeHandle representation_union =
            T.Union(representation1, representation2);
        TypeHandle semantic_union = T.Union(semantic1, semantic2);
        TypeHandle composed_union =
            T.Union(representation_union, semantic_union);
        CHECK(direct_union->Equals(composed_union));
      }
    }

    // Pointwiseness of Intersect.
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle representation1 = T.Representation(type1);
        TypeHandle semantic1 = T.Semantic(type1);
        TypeHandle representation2 = T.Representation(type2);
        TypeHandle semantic2 = T.Semantic(type2);
        TypeHandle direct_intersection = T.Intersect(type1, type2);
        TypeHandle representation_intersection =
            T.Intersect(representation1, representation2);
        TypeHandle semantic_intersection = T.Intersect(semantic1, semantic2);
        TypeHandle composed_intersection =
            T.Union(representation_intersection, semantic_intersection);
        CHECK(direct_intersection->Equals(composed_intersection));
      }
    }

    // Pointwiseness of Is.
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle representation1 = T.Representation(type1);
        TypeHandle semantic1 = T.Semantic(type1);
        TypeHandle representation2 = T.Representation(type2);
        TypeHandle semantic2 = T.Semantic(type2);
        bool representation_is = representation1->Is(representation2);
        bool semantic_is = semantic1->Is(semantic2);
        bool direct_is = type1->Is(type2);
        CHECK(direct_is == (semantic_is && representation_is));
      }
    }
  }

  void Class() {
    // Constructor
    for (MapIterator mt = T.maps.begin(); mt != T.maps.end(); ++mt) {
      Handle<i::Map> map = *mt;
      TypeHandle type = T.Class(map);
      CHECK(type->IsClass());
    }

    // Map attribute
    for (MapIterator mt = T.maps.begin(); mt != T.maps.end(); ++mt) {
      Handle<i::Map> map = *mt;
      TypeHandle type = T.Class(map);
      CHECK(*map == *type->AsClass()->Map());
    }

    // Functionality & Injectivity: Class(M1) = Class(M2) iff M1 = M2
    for (MapIterator mt1 = T.maps.begin(); mt1 != T.maps.end(); ++mt1) {
      for (MapIterator mt2 = T.maps.begin(); mt2 != T.maps.end(); ++mt2) {
        Handle<i::Map> map1 = *mt1;
        Handle<i::Map> map2 = *mt2;
        TypeHandle type1 = T.Class(map1);
        TypeHandle type2 = T.Class(map2);
        CHECK(Equal(type1, type2) == (*map1 == *map2));
      }
    }
  }

  void Constant() {
    // Constructor
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      Handle<i::Object> value = *vt;
      TypeHandle type = T.Constant(value);
      CHECK(type->IsConstant());
    }

    // Value attribute
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      Handle<i::Object> value = *vt;
      TypeHandle type = T.Constant(value);
      CHECK(*value == *type->AsConstant()->Value());
    }

    // Functionality & Injectivity: Constant(V1) = Constant(V2) iff V1 = V2
    for (ValueIterator vt1 = T.values.begin(); vt1 != T.values.end(); ++vt1) {
      for (ValueIterator vt2 = T.values.begin(); vt2 != T.values.end(); ++vt2) {
        Handle<i::Object> value1 = *vt1;
        Handle<i::Object> value2 = *vt2;
        TypeHandle type1 = T.Constant(value1);
        TypeHandle type2 = T.Constant(value2);
        CHECK(Equal(type1, type2) == (*value1 == *value2));
      }
    }

    // Typing of numbers
    Factory* fac = isolate->factory();
    CHECK(T.Constant(fac->NewNumber(0))->Is(T.UnsignedSmall));
    CHECK(T.Constant(fac->NewNumber(1))->Is(T.UnsignedSmall));
    CHECK(T.Constant(fac->NewNumber(0x3fffffff))->Is(T.UnsignedSmall));
    CHECK(T.Constant(fac->NewNumber(-1))->Is(T.Negative31));
    CHECK(T.Constant(fac->NewNumber(-0x3fffffff))->Is(T.Negative31));
    CHECK(T.Constant(fac->NewNumber(-0x40000000))->Is(T.Negative31));
    CHECK(T.Constant(fac->NewNumber(0x40000000))->Is(T.Unsigned31));
    CHECK(!T.Constant(fac->NewNumber(0x40000000))->Is(T.Unsigned30));
    CHECK(T.Constant(fac->NewNumber(0x7fffffff))->Is(T.Unsigned31));
    CHECK(!T.Constant(fac->NewNumber(0x7fffffff))->Is(T.Unsigned30));
    CHECK(T.Constant(fac->NewNumber(-0x40000001))->Is(T.Negative32));
    CHECK(!T.Constant(fac->NewNumber(-0x40000001))->Is(T.Negative31));
    CHECK(T.Constant(fac->NewNumber(-0x7fffffff))->Is(T.Negative32));
    CHECK(!T.Constant(fac->NewNumber(-0x7fffffff - 1))->Is(T.Negative31));
    if (SmiValuesAre31Bits()) {
      CHECK(!T.Constant(fac->NewNumber(0x40000000))->Is(T.UnsignedSmall));
      CHECK(!T.Constant(fac->NewNumber(0x7fffffff))->Is(T.UnsignedSmall));
      CHECK(!T.Constant(fac->NewNumber(-0x40000001))->Is(T.SignedSmall));
      CHECK(!T.Constant(fac->NewNumber(-0x7fffffff - 1))->Is(T.SignedSmall));
    } else {
      CHECK(SmiValuesAre32Bits());
      CHECK(T.Constant(fac->NewNumber(0x40000000))->Is(T.UnsignedSmall));
      CHECK(T.Constant(fac->NewNumber(0x7fffffff))->Is(T.UnsignedSmall));
      CHECK(T.Constant(fac->NewNumber(-0x40000001))->Is(T.SignedSmall));
      CHECK(T.Constant(fac->NewNumber(-0x7fffffff - 1))->Is(T.SignedSmall));
    }
    CHECK(T.Constant(fac->NewNumber(0x80000000u))->Is(T.Unsigned32));
    CHECK(!T.Constant(fac->NewNumber(0x80000000u))->Is(T.Unsigned31));
    CHECK(T.Constant(fac->NewNumber(0xffffffffu))->Is(T.Unsigned32));
    CHECK(!T.Constant(fac->NewNumber(0xffffffffu))->Is(T.Unsigned31));
    CHECK(T.Constant(fac->NewNumber(0xffffffffu + 1.0))->Is(T.PlainNumber));
    CHECK(!T.Constant(fac->NewNumber(0xffffffffu + 1.0))->Is(T.Integral32));
    CHECK(T.Constant(fac->NewNumber(-0x7fffffff - 2.0))->Is(T.PlainNumber));
    CHECK(!T.Constant(fac->NewNumber(-0x7fffffff - 2.0))->Is(T.Integral32));
    CHECK(T.Constant(fac->NewNumber(0.1))->Is(T.PlainNumber));
    CHECK(!T.Constant(fac->NewNumber(0.1))->Is(T.Integral32));
    CHECK(T.Constant(fac->NewNumber(-10.1))->Is(T.PlainNumber));
    CHECK(!T.Constant(fac->NewNumber(-10.1))->Is(T.Integral32));
    CHECK(T.Constant(fac->NewNumber(10e60))->Is(T.PlainNumber));
    CHECK(!T.Constant(fac->NewNumber(10e60))->Is(T.Integral32));
    CHECK(T.Constant(fac->NewNumber(-1.0*0.0))->Is(T.MinusZero));
    CHECK(T.Constant(fac->NewNumber(std::numeric_limits<double>::quiet_NaN()))
              ->Is(T.NaN));
    CHECK(T.Constant(fac->NewNumber(V8_INFINITY))->Is(T.PlainNumber));
    CHECK(!T.Constant(fac->NewNumber(V8_INFINITY))->Is(T.Integral32));
    CHECK(T.Constant(fac->NewNumber(-V8_INFINITY))->Is(T.PlainNumber));
    CHECK(!T.Constant(fac->NewNumber(-V8_INFINITY))->Is(T.Integral32));
  }

  void Range() {
    // Constructor
    for (ValueIterator i = T.integers.begin(); i != T.integers.end(); ++i) {
      for (ValueIterator j = T.integers.begin(); j != T.integers.end(); ++j) {
        double min = (*i)->Number();
        double max = (*j)->Number();
        if (min > max) std::swap(min, max);
        TypeHandle type = T.Range(min, max);
        CHECK(type->IsRange());
      }
    }

    // Range attributes
    for (ValueIterator i = T.integers.begin(); i != T.integers.end(); ++i) {
      for (ValueIterator j = T.integers.begin(); j != T.integers.end(); ++j) {
        double min = (*i)->Number();
        double max = (*j)->Number();
        if (min > max) std::swap(min, max);
        TypeHandle type = T.Range(min, max);
        CHECK(min == type->AsRange()->Min());
        CHECK(max == type->AsRange()->Max());
      }
    }

    // Functionality & Injectivity:
    // Range(min1, max1) = Range(min2, max2) <=> min1 = min2 /\ max1 = max2
    for (ValueIterator i1 = T.integers.begin();
        i1 != T.integers.end(); ++i1) {
      for (ValueIterator j1 = i1;
          j1 != T.integers.end(); ++j1) {
        for (ValueIterator i2 = T.integers.begin();
            i2 != T.integers.end(); ++i2) {
          for (ValueIterator j2 = i2;
              j2 != T.integers.end(); ++j2) {
            double min1 = (*i1)->Number();
            double max1 = (*j1)->Number();
            double min2 = (*i2)->Number();
            double max2 = (*j2)->Number();
            if (min1 > max1) std::swap(min1, max1);
            if (min2 > max2) std::swap(min2, max2);
            TypeHandle type1 = T.Range(min1, max1);
            TypeHandle type2 = T.Range(min2, max2);
            CHECK(Equal(type1, type2) == (min1 == min2 && max1 == max2));
          }
        }
      }
    }
  }

  void Context() {
    // Constructor
    for (int i = 0; i < 20; ++i) {
      TypeHandle type = T.Random();
      TypeHandle context = T.Context(type);
      CHECK(context->Iscontext());
    }

    // Attributes
    for (int i = 0; i < 20; ++i) {
      TypeHandle type = T.Random();
      TypeHandle context = T.Context(type);
      CheckEqual(type, context->AsContext()->Outer());
    }

    // Functionality & Injectivity: Context(T1) = Context(T2) iff T1 = T2
    for (int i = 0; i < 20; ++i) {
      for (int j = 0; j < 20; ++j) {
        TypeHandle type1 = T.Random();
        TypeHandle type2 = T.Random();
        TypeHandle context1 = T.Context(type1);
        TypeHandle context2 = T.Context(type2);
        CHECK(Equal(context1, context2) == Equal(type1, type2));
      }
    }
  }

  void Array() {
    // Constructor
    for (int i = 0; i < 20; ++i) {
      TypeHandle type = T.Random();
      TypeHandle array = T.Array1(type);
      CHECK(array->IsArray());
    }

    // Attributes
    for (int i = 0; i < 20; ++i) {
      TypeHandle type = T.Random();
      TypeHandle array = T.Array1(type);
      CheckEqual(type, array->AsArray()->Element());
    }

    // Functionality & Injectivity: Array(T1) = Array(T2) iff T1 = T2
    for (int i = 0; i < 20; ++i) {
      for (int j = 0; j < 20; ++j) {
        TypeHandle type1 = T.Random();
        TypeHandle type2 = T.Random();
        TypeHandle array1 = T.Array1(type1);
        TypeHandle array2 = T.Array1(type2);
        CHECK(Equal(array1, array2) == Equal(type1, type2));
      }
    }
  }

  void Function() {
    // Constructors
    for (int i = 0; i < 20; ++i) {
      for (int j = 0; j < 20; ++j) {
        for (int k = 0; k < 20; ++k) {
          TypeHandle type1 = T.Random();
          TypeHandle type2 = T.Random();
          TypeHandle type3 = T.Random();
          TypeHandle function0 = T.Function0(type1, type2);
          TypeHandle function1 = T.Function1(type1, type2, type3);
          TypeHandle function2 = T.Function2(type1, type2, type3);
          CHECK(function0->IsFunction());
          CHECK(function1->IsFunction());
          CHECK(function2->IsFunction());
        }
      }
    }

    // Attributes
    for (int i = 0; i < 20; ++i) {
      for (int j = 0; j < 20; ++j) {
        for (int k = 0; k < 20; ++k) {
          TypeHandle type1 = T.Random();
          TypeHandle type2 = T.Random();
          TypeHandle type3 = T.Random();
          TypeHandle function0 = T.Function0(type1, type2);
          TypeHandle function1 = T.Function1(type1, type2, type3);
          TypeHandle function2 = T.Function2(type1, type2, type3);
          CHECK_EQ(0, function0->AsFunction()->Arity());
          CHECK_EQ(1, function1->AsFunction()->Arity());
          CHECK_EQ(2, function2->AsFunction()->Arity());
          CheckEqual(type1, function0->AsFunction()->Result());
          CheckEqual(type1, function1->AsFunction()->Result());
          CheckEqual(type1, function2->AsFunction()->Result());
          CheckEqual(type2, function0->AsFunction()->Receiver());
          CheckEqual(type2, function1->AsFunction()->Receiver());
          CheckEqual(T.Any, function2->AsFunction()->Receiver());
          CheckEqual(type3, function1->AsFunction()->Parameter(0));
          CheckEqual(type2, function2->AsFunction()->Parameter(0));
          CheckEqual(type3, function2->AsFunction()->Parameter(1));
        }
      }
    }

    // Functionality & Injectivity: Function(Ts1) = Function(Ts2) iff Ts1 = Ts2
    for (int i = 0; i < 20; ++i) {
      for (int j = 0; j < 20; ++j) {
        for (int k = 0; k < 20; ++k) {
          TypeHandle type1 = T.Random();
          TypeHandle type2 = T.Random();
          TypeHandle type3 = T.Random();
          TypeHandle function01 = T.Function0(type1, type2);
          TypeHandle function02 = T.Function0(type1, type3);
          TypeHandle function03 = T.Function0(type3, type2);
          TypeHandle function11 = T.Function1(type1, type2, type2);
          TypeHandle function12 = T.Function1(type1, type2, type3);
          TypeHandle function21 = T.Function2(type1, type2, type2);
          TypeHandle function22 = T.Function2(type1, type2, type3);
          TypeHandle function23 = T.Function2(type1, type3, type2);
          CHECK(Equal(function01, function02) == Equal(type2, type3));
          CHECK(Equal(function01, function03) == Equal(type1, type3));
          CHECK(Equal(function11, function12) == Equal(type2, type3));
          CHECK(Equal(function21, function22) == Equal(type2, type3));
          CHECK(Equal(function21, function23) == Equal(type2, type3));
        }
      }
    }
  }

  void Of() {
    // Constant(V)->Is(Of(V))
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      Handle<i::Object> value = *vt;
      TypeHandle const_type = T.Constant(value);
      TypeHandle of_type = T.Of(value);
      CHECK(const_type->Is(of_type));
    }

    // If Of(V)->Is(T), then Constant(V)->Is(T)
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
        Handle<i::Object> value = *vt;
        TypeHandle type = *it;
        TypeHandle const_type = T.Constant(value);
        TypeHandle of_type = T.Of(value);
        CHECK(!of_type->Is(type) || const_type->Is(type));
      }
    }

    // If Constant(V)->Is(T), then Of(V)->Is(T) or T->Maybe(Constant(V))
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
        Handle<i::Object> value = *vt;
        TypeHandle type = *it;
        TypeHandle const_type = T.Constant(value);
        TypeHandle of_type = T.Of(value);
        CHECK(!const_type->Is(type) ||
              of_type->Is(type) || type->Maybe(const_type));
      }
    }
  }

  void NowOf() {
    // Constant(V)->NowIs(NowOf(V))
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      Handle<i::Object> value = *vt;
      TypeHandle const_type = T.Constant(value);
      TypeHandle nowof_type = T.NowOf(value);
      CHECK(const_type->NowIs(nowof_type));
    }

    // NowOf(V)->Is(Of(V))
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      Handle<i::Object> value = *vt;
      TypeHandle nowof_type = T.NowOf(value);
      TypeHandle of_type = T.Of(value);
      CHECK(nowof_type->Is(of_type));
    }

    // If NowOf(V)->NowIs(T), then Constant(V)->NowIs(T)
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
        Handle<i::Object> value = *vt;
        TypeHandle type = *it;
        TypeHandle const_type = T.Constant(value);
        TypeHandle nowof_type = T.NowOf(value);
        CHECK(!nowof_type->NowIs(type) || const_type->NowIs(type));
      }
    }

    // If Constant(V)->NowIs(T),
    // then NowOf(V)->NowIs(T) or T->Maybe(Constant(V))
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
        Handle<i::Object> value = *vt;
        TypeHandle type = *it;
        TypeHandle const_type = T.Constant(value);
        TypeHandle nowof_type = T.NowOf(value);
        CHECK(!const_type->NowIs(type) ||
              nowof_type->NowIs(type) || type->Maybe(const_type));
      }
    }

    // If Constant(V)->Is(T),
    // then NowOf(V)->Is(T) or T->Maybe(Constant(V))
    for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
      for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
        Handle<i::Object> value = *vt;
        TypeHandle type = *it;
        TypeHandle const_type = T.Constant(value);
        TypeHandle nowof_type = T.NowOf(value);
        CHECK(!const_type->Is(type) ||
              nowof_type->Is(type) || type->Maybe(const_type));
      }
    }
  }

  void MinMax() {
    // If b is regular numeric bitset, then Range(b->Min(), b->Max())->Is(b).
    // TODO(neis): Need to ignore representation for this to be true.
    /*
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (this->IsBitset(type) && type->Is(T.Number) &&
          !type->Is(T.None) && !type->Is(T.NaN)) {
        TypeHandle range = T.Range(
            isolate->factory()->NewNumber(type->Min()),
            isolate->factory()->NewNumber(type->Max()));
        CHECK(range->Is(type));
      }
    }
    */

    // If b is regular numeric bitset, then b->Min() and b->Max() are integers.
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (this->IsBitset(type) && type->Is(T.Number) && !type->Is(T.NaN)) {
        CHECK(IsInteger(type->Min()) && IsInteger(type->Max()));
      }
    }

    // If b1 and b2 are regular numeric bitsets with b1->Is(b2), then
    // b1->Min() >= b2->Min() and b1->Max() <= b2->Max().
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        if (this->IsBitset(type1) && type1->Is(type2) && type2->Is(T.Number) &&
            !type1->Is(T.NaN) && !type2->Is(T.NaN)) {
          CHECK(type1->Min() >= type2->Min());
          CHECK(type1->Max() <= type2->Max());
        }
      }
    }

    // Lub(Range(x,y))->Min() <= x and y <= Lub(Range(x,y))->Max()
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (type->IsRange()) {
        TypeHandle lub = Rep::BitsetType::New(
            Rep::BitsetType::Lub(type), T.region());
        CHECK(lub->Min() <= type->Min() && type->Max() <= lub->Max());
      }
    }

    // Rangification: If T->Is(Range(-inf,+inf)) and T is inhabited, then
    // T->Is(Range(T->Min(), T->Max())).
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(!type->Is(T.Integer) || !type->IsInhabited() ||
            type->Is(T.Range(type->Min(), type->Max())));
    }
  }

  void BitsetGlb() {
    // Lower: (T->BitsetGlb())->Is(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle glb =
          Rep::BitsetType::New(Rep::BitsetType::Glb(type), T.region());
      CHECK(glb->Is(type));
    }

    // Greatest: If T1->IsBitset() and T1->Is(T2), then T1->Is(T2->BitsetGlb())
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle glb2 =
            Rep::BitsetType::New(Rep::BitsetType::Glb(type2), T.region());
        CHECK(!this->IsBitset(type1) || !type1->Is(type2) || type1->Is(glb2));
      }
    }

    // Monotonicity: T1->Is(T2) implies (T1->BitsetGlb())->Is(T2->BitsetGlb())
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle glb1 =
            Rep::BitsetType::New(Rep::BitsetType::Glb(type1), T.region());
        TypeHandle glb2 =
            Rep::BitsetType::New(Rep::BitsetType::Glb(type2), T.region());
        CHECK(!type1->Is(type2) || glb1->Is(glb2));
      }
    }
  }

  void BitsetLub() {
    // Upper: T->Is(T->BitsetLub())
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle lub =
          Rep::BitsetType::New(Rep::BitsetType::Lub(type), T.region());
      CHECK(type->Is(lub));
    }

    // Least: If T2->IsBitset() and T1->Is(T2), then (T1->BitsetLub())->Is(T2)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle lub1 =
            Rep::BitsetType::New(Rep::BitsetType::Lub(type1), T.region());
        CHECK(!this->IsBitset(type2) || !type1->Is(type2) || lub1->Is(type2));
      }
    }

    // Monotonicity: T1->Is(T2) implies (T1->BitsetLub())->Is(T2->BitsetLub())
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle lub1 =
            Rep::BitsetType::New(Rep::BitsetType::Lub(type1), T.region());
        TypeHandle lub2 =
            Rep::BitsetType::New(Rep::BitsetType::Lub(type2), T.region());
        CHECK(!type1->Is(type2) || lub1->Is(lub2));
      }
    }
  }

  void Is1() {
    // Least Element (Bottom): None->Is(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(T.None->Is(type));
    }

    // Greatest Element (Top): T->Is(Any)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(type->Is(T.Any));
    }

    // Bottom Uniqueness: T->Is(None) implies T = None
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (type->Is(T.None)) CheckEqual(type, T.None);
    }

    // Top Uniqueness: Any->Is(T) implies T = Any
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (T.Any->Is(type)) CheckEqual(type, T.Any);
    }

    // Reflexivity: T->Is(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(type->Is(type));
    }

    // Transitivity: T1->Is(T2) and T2->Is(T3) implies T1->Is(T3)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          CHECK(!(type1->Is(type2) && type2->Is(type3)) || type1->Is(type3));
        }
      }
    }

    // Antisymmetry: T1->Is(T2) and T2->Is(T1) iff T1 = T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        CHECK((type1->Is(type2) && type2->Is(type1)) == Equal(type1, type2));
      }
    }

    // (In-)Compatibilities.
    for (TypeIterator i = T.types.begin(); i != T.types.end(); ++i) {
      for (TypeIterator j = T.types.begin(); j != T.types.end(); ++j) {
        TypeHandle type1 = *i;
        TypeHandle type2 = *j;
        CHECK(!type1->Is(type2) || this->IsBitset(type2) ||
              this->IsUnion(type2) || this->IsUnion(type1) ||
              (type1->IsClass() && type2->IsClass()) ||
              (type1->IsConstant() && type2->IsConstant()) ||
              (type1->IsConstant() && type2->IsRange()) ||
              (this->IsBitset(type1) && type2->IsRange()) ||
              (type1->IsRange() && type2->IsRange()) ||
              (type1->IsContext() && type2->IsContext()) ||
              (type1->IsArray() && type2->IsArray()) ||
              (type1->IsFunction() && type2->IsFunction()) ||
              !type1->IsInhabited());
      }
    }
  }

  void Is2() {
    // Class(M1)->Is(Class(M2)) iff M1 = M2
    for (MapIterator mt1 = T.maps.begin(); mt1 != T.maps.end(); ++mt1) {
      for (MapIterator mt2 = T.maps.begin(); mt2 != T.maps.end(); ++mt2) {
        Handle<i::Map> map1 = *mt1;
        Handle<i::Map> map2 = *mt2;
        TypeHandle class_type1 = T.Class(map1);
        TypeHandle class_type2 = T.Class(map2);
        CHECK(class_type1->Is(class_type2) == (*map1 == *map2));
      }
    }

    // Range(X1, Y1)->Is(Range(X2, Y2)) iff X1 >= X2 /\ Y1 <= Y2
    for (ValueIterator i1 = T.integers.begin();
        i1 != T.integers.end(); ++i1) {
      for (ValueIterator j1 = i1;
          j1 != T.integers.end(); ++j1) {
        for (ValueIterator i2 = T.integers.begin();
             i2 != T.integers.end(); ++i2) {
          for (ValueIterator j2 = i2;
               j2 != T.integers.end(); ++j2) {
            double min1 = (*i1)->Number();
            double max1 = (*j1)->Number();
            double min2 = (*i2)->Number();
            double max2 = (*j2)->Number();
            if (min1 > max1) std::swap(min1, max1);
            if (min2 > max2) std::swap(min2, max2);
            TypeHandle type1 = T.Range(min1, max1);
            TypeHandle type2 = T.Range(min2, max2);
            CHECK(type1->Is(type2) == (min1 >= min2 && max1 <= max2));
          }
        }
      }
    }

    // Constant(V1)->Is(Constant(V2)) iff V1 = V2
    for (ValueIterator vt1 = T.values.begin(); vt1 != T.values.end(); ++vt1) {
      for (ValueIterator vt2 = T.values.begin(); vt2 != T.values.end(); ++vt2) {
        Handle<i::Object> value1 = *vt1;
        Handle<i::Object> value2 = *vt2;
        TypeHandle const_type1 = T.Constant(value1);
        TypeHandle const_type2 = T.Constant(value2);
        CHECK(const_type1->Is(const_type2) == (*value1 == *value2));
      }
    }

    // Context(T1)->Is(Context(T2)) iff T1 = T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle outer1 = *it1;
        TypeHandle outer2 = *it2;
        TypeHandle type1 = T.Context(outer1);
        TypeHandle type2 = T.Context(outer2);
        CHECK(type1->Is(type2) == outer1->Equals(outer2));
      }
    }

    // Array(T1)->Is(Array(T2)) iff T1 = T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle element1 = *it1;
        TypeHandle element2 = *it2;
        TypeHandle type1 = T.Array1(element1);
        TypeHandle type2 = T.Array1(element2);
        CHECK(type1->Is(type2) == element1->Equals(element2));
      }
    }

    // Function0(S1, T1)->Is(Function0(S2, T2)) iff S1 = S2 and T1 = T2
    for (TypeIterator i = T.types.begin(); i != T.types.end(); ++i) {
      for (TypeIterator j = T.types.begin(); j != T.types.end(); ++j) {
        TypeHandle result1 = *i;
        TypeHandle receiver1 = *j;
        TypeHandle type1 = T.Function0(result1, receiver1);
        TypeHandle result2 = T.Random();
        TypeHandle receiver2 = T.Random();
        TypeHandle type2 = T.Function0(result2, receiver2);
        CHECK(type1->Is(type2) ==
            (result1->Equals(result2) && receiver1->Equals(receiver2)));
      }
    }


    // Range-specific subtyping

    // If IsInteger(v) then Constant(v)->Is(Range(v, v)).
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (type->IsConstant() && IsInteger(*type->AsConstant()->Value())) {
        CHECK(type->Is(T.Range(type->AsConstant()->Value()->Number(),
                               type->AsConstant()->Value()->Number())));
      }
    }

    // If Constant(x)->Is(Range(min,max)) then IsInteger(v) and min <= x <= max.
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        if (type1->IsConstant() && type2->IsRange() && type1->Is(type2)) {
          double x = type1->AsConstant()->Value()->Number();
          double min = type2->AsRange()->Min();
          double max = type2->AsRange()->Max();
          CHECK(IsInteger(x) && min <= x && x <= max);
        }
      }
    }

    // Lub(Range(x,y))->Is(T.Union(T.Integral32, T.OtherNumber))
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (type->IsRange()) {
        TypeHandle lub = Rep::BitsetType::New(
            Rep::BitsetType::Lub(type), T.region());
        CHECK(lub->Is(T.PlainNumber));
      }
    }


    // Subtyping between concrete basic types

    CheckUnordered(T.Boolean, T.Null);
    CheckUnordered(T.Undefined, T.Null);
    CheckUnordered(T.Boolean, T.Undefined);

    CheckSub(T.SignedSmall, T.Number);
    CheckSub(T.Signed32, T.Number);
    CheckSubOrEqual(T.SignedSmall, T.Signed32);
    CheckUnordered(T.SignedSmall, T.MinusZero);
    CheckUnordered(T.Signed32, T.Unsigned32);

    CheckSub(T.UniqueName, T.Name);
    CheckSub(T.String, T.Name);
    CheckSub(T.InternalizedString, T.String);
    CheckSub(T.InternalizedString, T.UniqueName);
    CheckSub(T.InternalizedString, T.Name);
    CheckSub(T.Symbol, T.UniqueName);
    CheckSub(T.Symbol, T.Name);
    CheckUnordered(T.String, T.UniqueName);
    CheckUnordered(T.String, T.Symbol);
    CheckUnordered(T.InternalizedString, T.Symbol);

    CheckSub(T.Object, T.Receiver);
    CheckSub(T.Proxy, T.Receiver);
    CheckSub(T.OtherObject, T.Object);
    CheckSub(T.Undetectable, T.Object);
    CheckSub(T.DetectableObject, T.Object);
    CheckSub(T.GlobalObject, T.DetectableObject);
    CheckSub(T.OtherObject, T.DetectableObject);
    CheckSub(T.GlobalObject, T.Object);
    CheckSub(T.GlobalObject, T.Receiver);

    CheckUnordered(T.Object, T.Proxy);
    CheckUnordered(T.GlobalObject, T.OtherObject);
    CheckUnordered(T.DetectableObject, T.Undetectable);

    // Subtyping between concrete structural types

    CheckSub(T.ObjectClass, T.Object);
    CheckSub(T.ArrayClass, T.OtherObject);
    CheckSub(T.UninitializedClass, T.Internal);
    CheckUnordered(T.ObjectClass, T.ArrayClass);
    CheckUnordered(T.UninitializedClass, T.Null);
    CheckUnordered(T.UninitializedClass, T.Undefined);

    CheckSub(T.SmiConstant, T.SignedSmall);
    CheckSub(T.SmiConstant, T.Signed32);
    CheckSub(T.SmiConstant, T.Number);
    CheckSub(T.ObjectConstant1, T.Object);
    CheckSub(T.ObjectConstant2, T.Object);
    CheckSub(T.ArrayConstant, T.Object);
    CheckSub(T.ArrayConstant, T.OtherObject);
    CheckSub(T.ArrayConstant, T.Receiver);
    CheckSub(T.UninitializedConstant, T.Internal);
    CheckUnordered(T.ObjectConstant1, T.ObjectConstant2);
    CheckUnordered(T.ObjectConstant1, T.ArrayConstant);
    CheckUnordered(T.UninitializedConstant, T.Null);
    CheckUnordered(T.UninitializedConstant, T.Undefined);

    CheckUnordered(T.ObjectConstant1, T.ObjectClass);
    CheckUnordered(T.ObjectConstant2, T.ObjectClass);
    CheckUnordered(T.ObjectConstant1, T.ArrayClass);
    CheckUnordered(T.ObjectConstant2, T.ArrayClass);
    CheckUnordered(T.ArrayConstant, T.ObjectClass);

    CheckSub(T.NumberArray, T.OtherObject);
    CheckSub(T.NumberArray, T.Receiver);
    CheckSub(T.NumberArray, T.Object);
    CheckUnordered(T.StringArray, T.AnyArray);

    CheckSub(T.MethodFunction, T.Object);
    CheckSub(T.NumberFunction1, T.Object);
    CheckUnordered(T.SignedFunction1, T.NumberFunction1);
    CheckUnordered(T.NumberFunction1, T.NumberFunction2);
  }

  void NowIs() {
    // Least Element (Bottom): None->NowIs(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(T.None->NowIs(type));
    }

    // Greatest Element (Top): T->NowIs(Any)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(type->NowIs(T.Any));
    }

    // Bottom Uniqueness: T->NowIs(None) implies T = None
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (type->NowIs(T.None)) CheckEqual(type, T.None);
    }

    // Top Uniqueness: Any->NowIs(T) implies T = Any
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      if (T.Any->NowIs(type)) CheckEqual(type, T.Any);
    }

    // Reflexivity: T->NowIs(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(type->NowIs(type));
    }

    // Transitivity: T1->NowIs(T2) and T2->NowIs(T3) implies T1->NowIs(T3)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          CHECK(!(type1->NowIs(type2) && type2->NowIs(type3)) ||
                type1->NowIs(type3));
        }
      }
    }

    // Antisymmetry: T1->NowIs(T2) and T2->NowIs(T1) iff T1 = T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        CHECK((type1->NowIs(type2) && type2->NowIs(type1)) ==
              Equal(type1, type2));
      }
    }

    // T1->Is(T2) implies T1->NowIs(T2)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        CHECK(!type1->Is(type2) || type1->NowIs(type2));
      }
    }

    // Constant(V1)->NowIs(Constant(V2)) iff V1 = V2
    for (ValueIterator vt1 = T.values.begin(); vt1 != T.values.end(); ++vt1) {
      for (ValueIterator vt2 = T.values.begin(); vt2 != T.values.end(); ++vt2) {
        Handle<i::Object> value1 = *vt1;
        Handle<i::Object> value2 = *vt2;
        TypeHandle const_type1 = T.Constant(value1);
        TypeHandle const_type2 = T.Constant(value2);
        CHECK(const_type1->NowIs(const_type2) == (*value1 == *value2));
      }
    }

    // Class(M1)->NowIs(Class(M2)) iff M1 = M2
    for (MapIterator mt1 = T.maps.begin(); mt1 != T.maps.end(); ++mt1) {
      for (MapIterator mt2 = T.maps.begin(); mt2 != T.maps.end(); ++mt2) {
        Handle<i::Map> map1 = *mt1;
        Handle<i::Map> map2 = *mt2;
        TypeHandle class_type1 = T.Class(map1);
        TypeHandle class_type2 = T.Class(map2);
        CHECK(class_type1->NowIs(class_type2) == (*map1 == *map2));
      }
    }

    // Constant(V)->NowIs(Class(M)) iff V has map M
    for (MapIterator mt = T.maps.begin(); mt != T.maps.end(); ++mt) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        Handle<i::Map> map = *mt;
        Handle<i::Object> value = *vt;
        TypeHandle const_type = T.Constant(value);
        TypeHandle class_type = T.Class(map);
        CHECK((value->IsHeapObject() &&
               i::HeapObject::cast(*value)->map() == *map)
              == const_type->NowIs(class_type));
      }
    }

    // Class(M)->NowIs(Constant(V)) never
    for (MapIterator mt = T.maps.begin(); mt != T.maps.end(); ++mt) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        Handle<i::Map> map = *mt;
        Handle<i::Object> value = *vt;
        TypeHandle const_type = T.Constant(value);
        TypeHandle class_type = T.Class(map);
        CHECK(!class_type->NowIs(const_type));
      }
    }
  }

  void Contains() {
    // T->Contains(V) iff Constant(V)->Is(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        TypeHandle type = *it;
        Handle<i::Object> value = *vt;
        TypeHandle const_type = T.Constant(value);
        CHECK(type->Contains(value) == const_type->Is(type));
      }
    }
  }

  void NowContains() {
    // T->NowContains(V) iff Constant(V)->NowIs(T)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        TypeHandle type = *it;
        Handle<i::Object> value = *vt;
        TypeHandle const_type = T.Constant(value);
        CHECK(type->NowContains(value) == const_type->NowIs(type));
      }
    }

    // T->Contains(V) implies T->NowContains(V)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        TypeHandle type = *it;
        Handle<i::Object> value = *vt;
        CHECK(!type->Contains(value) || type->NowContains(value));
      }
    }

    // NowOf(V)->Is(T) implies T->NowContains(V)
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        TypeHandle type = *it;
        Handle<i::Object> value = *vt;
        TypeHandle nowof_type = T.Of(value);
        CHECK(!nowof_type->NowIs(type) || type->NowContains(value));
      }
    }
  }

  void Maybe() {
    // T->Maybe(Any) iff T inhabited
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(type->Maybe(T.Any) == type->IsInhabited());
    }

    // T->Maybe(None) never
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(!type->Maybe(T.None));
    }

    // Reflexivity upto Inhabitation: T->Maybe(T) iff T inhabited
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      CHECK(type->Maybe(type) == type->IsInhabited());
    }

    // Symmetry: T1->Maybe(T2) iff T2->Maybe(T1)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        CHECK(type1->Maybe(type2) == type2->Maybe(type1));
      }
    }

    // T1->Maybe(T2) implies T1, T2 inhabited
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        CHECK(!type1->Maybe(type2) ||
              (type1->IsInhabited() && type2->IsInhabited()));
      }
    }

    // T1->Maybe(T2) implies Intersect(T1, T2) inhabited
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle intersect12 = T.Intersect(type1, type2);
        CHECK(!type1->Maybe(type2) || intersect12->IsInhabited());
      }
    }

    // T1->Is(T2) and T1 inhabited implies T1->Maybe(T2)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        CHECK(!(type1->Is(type2) && type1->IsInhabited()) ||
              type1->Maybe(type2));
      }
    }

    // Constant(V1)->Maybe(Constant(V2)) iff V1 = V2
    for (ValueIterator vt1 = T.values.begin(); vt1 != T.values.end(); ++vt1) {
      for (ValueIterator vt2 = T.values.begin(); vt2 != T.values.end(); ++vt2) {
        Handle<i::Object> value1 = *vt1;
        Handle<i::Object> value2 = *vt2;
        TypeHandle const_type1 = T.Constant(value1);
        TypeHandle const_type2 = T.Constant(value2);
        CHECK(const_type1->Maybe(const_type2) == (*value1 == *value2));
      }
    }

    // Class(M1)->Maybe(Class(M2)) iff M1 = M2
    for (MapIterator mt1 = T.maps.begin(); mt1 != T.maps.end(); ++mt1) {
      for (MapIterator mt2 = T.maps.begin(); mt2 != T.maps.end(); ++mt2) {
        Handle<i::Map> map1 = *mt1;
        Handle<i::Map> map2 = *mt2;
        TypeHandle class_type1 = T.Class(map1);
        TypeHandle class_type2 = T.Class(map2);
        CHECK(class_type1->Maybe(class_type2) == (*map1 == *map2));
      }
    }

    // Constant(V)->Maybe(Class(M)) never
    // This does NOT hold!
    /*
    for (MapIterator mt = T.maps.begin(); mt != T.maps.end(); ++mt) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        Handle<i::Map> map = *mt;
        Handle<i::Object> value = *vt;
        TypeHandle const_type = T.Constant(value);
        TypeHandle class_type = T.Class(map);
        CHECK(!const_type->Maybe(class_type));
      }
    }
    */

    // Class(M)->Maybe(Constant(V)) never
    // This does NOT hold!
    /*
    for (MapIterator mt = T.maps.begin(); mt != T.maps.end(); ++mt) {
      for (ValueIterator vt = T.values.begin(); vt != T.values.end(); ++vt) {
        Handle<i::Map> map = *mt;
        Handle<i::Object> value = *vt;
        TypeHandle const_type = T.Constant(value);
        TypeHandle class_type = T.Class(map);
        CHECK(!class_type->Maybe(const_type));
      }
    }
    */

    // Basic types
    CheckDisjoint(T.Boolean, T.Null);
    CheckDisjoint(T.Undefined, T.Null);
    CheckDisjoint(T.Boolean, T.Undefined);
    CheckOverlap(T.SignedSmall, T.Number);
    CheckOverlap(T.NaN, T.Number);
    CheckDisjoint(T.Signed32, T.NaN);
    CheckOverlap(T.UniqueName, T.Name);
    CheckOverlap(T.String, T.Name);
    CheckOverlap(T.InternalizedString, T.String);
    CheckOverlap(T.InternalizedString, T.UniqueName);
    CheckOverlap(T.InternalizedString, T.Name);
    CheckOverlap(T.Symbol, T.UniqueName);
    CheckOverlap(T.Symbol, T.Name);
    CheckOverlap(T.String, T.UniqueName);
    CheckDisjoint(T.String, T.Symbol);
    CheckDisjoint(T.InternalizedString, T.Symbol);
    CheckOverlap(T.Object, T.Receiver);
    CheckOverlap(T.OtherObject, T.Object);
    CheckOverlap(T.GlobalObject, T.Object);
    CheckOverlap(T.Proxy, T.Receiver);
    CheckDisjoint(T.Object, T.Proxy);

    // Structural types
    CheckOverlap(T.ObjectClass, T.Object);
    CheckOverlap(T.ArrayClass, T.Object);
    CheckOverlap(T.ObjectClass, T.ObjectClass);
    CheckOverlap(T.ArrayClass, T.ArrayClass);
    CheckDisjoint(T.ObjectClass, T.ArrayClass);
    CheckOverlap(T.SmiConstant, T.SignedSmall);
    CheckOverlap(T.SmiConstant, T.Signed32);
    CheckOverlap(T.SmiConstant, T.Number);
    CheckOverlap(T.ObjectConstant1, T.Object);
    CheckOverlap(T.ObjectConstant2, T.Object);
    CheckOverlap(T.ArrayConstant, T.Object);
    CheckOverlap(T.ArrayConstant, T.Receiver);
    CheckOverlap(T.ObjectConstant1, T.ObjectConstant1);
    CheckDisjoint(T.ObjectConstant1, T.ObjectConstant2);
    CheckDisjoint(T.ObjectConstant1, T.ArrayConstant);
    CheckOverlap(T.ObjectConstant1, T.ArrayClass);
    CheckOverlap(T.ObjectConstant2, T.ArrayClass);
    CheckOverlap(T.ArrayConstant, T.ObjectClass);
    CheckOverlap(T.NumberArray, T.Receiver);
    CheckDisjoint(T.NumberArray, T.AnyArray);
    CheckDisjoint(T.NumberArray, T.StringArray);
    CheckOverlap(T.MethodFunction, T.Object);
    CheckDisjoint(T.SignedFunction1, T.NumberFunction1);
    CheckDisjoint(T.SignedFunction1, T.NumberFunction2);
    CheckDisjoint(T.NumberFunction1, T.NumberFunction2);
    CheckDisjoint(T.SignedFunction1, T.MethodFunction);
    CheckOverlap(T.ObjectConstant1, T.ObjectClass);  // !!!
    CheckOverlap(T.ObjectConstant2, T.ObjectClass);  // !!!
    CheckOverlap(T.NumberClass, T.Intersect(T.Number, T.Tagged));  // !!!
  }

  void Union1() {
    // Identity: Union(T, None) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle union_type = T.Union(type, T.None);
      CheckEqual(union_type, type);
    }

    // Domination: Union(T, Any) = Any
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle union_type = T.Union(type, T.Any);
      CheckEqual(union_type, T.Any);
    }

    // Idempotence: Union(T, T) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle union_type = T.Union(type, type);
      CheckEqual(union_type, type);
    }

    // Commutativity: Union(T1, T2) = Union(T2, T1)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle union12 = T.Union(type1, type2);
        TypeHandle union21 = T.Union(type2, type1);
        CheckEqual(union12, union21);
      }
    }

    // Associativity: Union(T1, Union(T2, T3)) = Union(Union(T1, T2), T3)
    // This does NOT hold!  For example:
    // (Unsigned32 \/ Range(0,5)) \/ Range(-5,0) = Unsigned32 \/ Range(-5,0)
    // Unsigned32 \/ (Range(0,5) \/ Range(-5,0)) = Unsigned32 \/ Range(-5,5)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle union12 = T.Union(type1, type2);
          TypeHandle union23 = T.Union(type2, type3);
          TypeHandle union1_23 = T.Union(type1, union23);
          TypeHandle union12_3 = T.Union(union12, type3);
          CheckEqual(union1_23, union12_3);
        }
      }
    }
    */

    // Meet: T1->Is(Union(T1, T2)) and T2->Is(Union(T1, T2))
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle union12 = T.Union(type1, type2);
        CHECK(type1->Is(union12));
        CHECK(type2->Is(union12));
      }
    }

    // Upper Boundedness: T1->Is(T2) implies Union(T1, T2) = T2
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle union12 = T.Union(type1, type2);
        if (type1->Is(type2)) CheckEqual(union12, type2);
      }
    }

    // Monotonicity: T1->Is(T2) implies Union(T1, T3)->Is(Union(T2, T3))
    // This does NOT hold.  For example:
    // Range(-5,-1) <= Signed32
    // Range(-5,-1) \/ Range(1,5) = Range(-5,5) </= Signed32 \/ Range(1,5)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle union13 = T.Union(type1, type3);
          TypeHandle union23 = T.Union(type2, type3);
          CHECK(!type1->Is(type2) || union13->Is(union23));
        }
      }
    }
    */
  }

  void Union2() {
    // Monotonicity: T1->Is(T3) and T2->Is(T3) implies Union(T1, T2)->Is(T3)
    // This does NOT hold.  For example:
    // Range(-2^33, -2^33) <= OtherNumber
    // Range(2^33, 2^33) <= OtherNumber
    // Range(-2^33, 2^33) </= OtherNumber
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle union12 = T.Union(type1, type2);
          CHECK(!(type1->Is(type3) && type2->Is(type3)) || union12->Is(type3));
        }
      }
    }
    */
  }

  void Union3() {
    // Monotonicity: T1->Is(T2) or T1->Is(T3) implies T1->Is(Union(T2, T3))
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = it2; it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle union23 = T.Union(type2, type3);
          CHECK(!(type1->Is(type2) || type1->Is(type3)) || type1->Is(union23));
        }
      }
    }
  }

  void Union4() {
    // Class-class
    CheckSub(T.Union(T.ObjectClass, T.ArrayClass), T.Object);
    CheckOverlap(T.Union(T.ObjectClass, T.ArrayClass), T.OtherObject);
    CheckOverlap(T.Union(T.ObjectClass, T.ArrayClass), T.Receiver);
    CheckDisjoint(T.Union(T.ObjectClass, T.ArrayClass), T.Number);

    // Constant-constant
    CheckSub(T.Union(T.ObjectConstant1, T.ObjectConstant2), T.Object);
    CheckOverlap(T.Union(T.ObjectConstant1, T.ArrayConstant), T.OtherObject);
    CheckUnordered(
        T.Union(T.ObjectConstant1, T.ObjectConstant2), T.ObjectClass);
    CheckOverlap(T.Union(T.ObjectConstant1, T.ArrayConstant), T.OtherObject);
    CheckDisjoint(
        T.Union(T.ObjectConstant1, T.ArrayConstant), T.Number);
    CheckOverlap(
        T.Union(T.ObjectConstant1, T.ArrayConstant), T.ObjectClass);  // !!!

    // Bitset-array
    CHECK(this->IsBitset(T.Union(T.AnyArray, T.Receiver)));
    CHECK(this->IsUnion(T.Union(T.NumberArray, T.Number)));

    CheckEqual(T.Union(T.AnyArray, T.Receiver), T.Receiver);
    CheckEqual(T.Union(T.AnyArray, T.OtherObject), T.OtherObject);
    CheckUnordered(T.Union(T.AnyArray, T.String), T.Receiver);
    CheckOverlap(T.Union(T.NumberArray, T.String), T.Object);
    CheckDisjoint(T.Union(T.NumberArray, T.String), T.Number);

    // Bitset-function
    CHECK(this->IsBitset(T.Union(T.MethodFunction, T.Object)));
    CHECK(this->IsUnion(T.Union(T.NumberFunction1, T.Number)));

    CheckEqual(T.Union(T.MethodFunction, T.Object), T.Object);
    CheckUnordered(T.Union(T.NumberFunction1, T.String), T.Object);
    CheckOverlap(T.Union(T.NumberFunction2, T.String), T.Object);
    CheckDisjoint(T.Union(T.NumberFunction1, T.String), T.Number);

    // Bitset-class
    CheckSub(T.Union(T.ObjectClass, T.SignedSmall),
             T.Union(T.Object, T.Number));
    CheckSub(T.Union(T.ObjectClass, T.OtherObject), T.Object);
    CheckUnordered(T.Union(T.ObjectClass, T.String), T.OtherObject);
    CheckOverlap(T.Union(T.ObjectClass, T.String), T.Object);
    CheckDisjoint(T.Union(T.ObjectClass, T.String), T.Number);

    // Bitset-constant
    CheckSub(
        T.Union(T.ObjectConstant1, T.Signed32), T.Union(T.Object, T.Number));
    CheckSub(T.Union(T.ObjectConstant1, T.OtherObject), T.Object);
    CheckUnordered(T.Union(T.ObjectConstant1, T.String), T.OtherObject);
    CheckOverlap(T.Union(T.ObjectConstant1, T.String), T.Object);
    CheckDisjoint(T.Union(T.ObjectConstant1, T.String), T.Number);

    // Class-constant
    CheckSub(T.Union(T.ObjectConstant1, T.ArrayClass), T.Object);
    CheckUnordered(T.ObjectClass, T.Union(T.ObjectConstant1, T.ArrayClass));
    CheckSub(T.Union(T.ObjectConstant1, T.ArrayClass),
             T.Union(T.Receiver, T.Object));
    CheckUnordered(T.Union(T.ObjectConstant1, T.ArrayClass), T.ArrayConstant);
    CheckOverlap(T.Union(T.ObjectConstant1, T.ArrayClass), T.ObjectConstant2);
    CheckOverlap(
        T.Union(T.ObjectConstant1, T.ArrayClass), T.ObjectClass);  // !!!

    // Bitset-union
    CheckSub(
        T.NaN,
        T.Union(T.Union(T.ArrayClass, T.ObjectConstant1), T.Number));
    CheckSub(
        T.Union(T.Union(T.ArrayClass, T.ObjectConstant1), T.Signed32),
        T.Union(T.ObjectConstant1, T.Union(T.Number, T.ArrayClass)));

    // Class-union
    CheckSub(
        T.Union(T.ObjectClass, T.Union(T.ObjectConstant1, T.ObjectClass)),
        T.Object);
    CheckEqual(
        T.Union(T.Union(T.ArrayClass, T.ObjectConstant2), T.ArrayClass),
        T.Union(T.ArrayClass, T.ObjectConstant2));

    // Constant-union
    CheckEqual(
        T.Union(
            T.ObjectConstant1, T.Union(T.ObjectConstant1, T.ObjectConstant2)),
        T.Union(T.ObjectConstant2, T.ObjectConstant1));
    CheckEqual(
        T.Union(
            T.Union(T.ArrayConstant, T.ObjectConstant2), T.ObjectConstant1),
        T.Union(
            T.ObjectConstant2, T.Union(T.ArrayConstant, T.ObjectConstant1)));

    // Array-union
    CheckEqual(
        T.Union(T.AnyArray, T.Union(T.NumberArray, T.AnyArray)),
        T.Union(T.AnyArray, T.NumberArray));
    CheckSub(T.Union(T.AnyArray, T.NumberArray), T.OtherObject);

    // Function-union
    CheckEqual(
        T.Union(T.NumberFunction1, T.NumberFunction2),
        T.Union(T.NumberFunction2, T.NumberFunction1));
    CheckSub(T.Union(T.SignedFunction1, T.MethodFunction), T.Object);

    // Union-union
    CheckEqual(
        T.Union(
            T.Union(T.ObjectConstant2, T.ObjectConstant1),
            T.Union(T.ObjectConstant1, T.ObjectConstant2)),
        T.Union(T.ObjectConstant2, T.ObjectConstant1));
    CheckEqual(T.Union(T.Union(T.Number, T.ArrayClass),
                       T.Union(T.SignedSmall, T.Receiver)),
               T.Union(T.Number, T.Receiver));
  }

  void Intersect() {
    // Identity: Intersect(T, Any) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle intersect_type = T.Intersect(type, T.Any);
      CheckEqual(intersect_type, type);
    }

    // Domination: Intersect(T, None) = None
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle intersect_type = T.Intersect(type, T.None);
      CheckEqual(intersect_type, T.None);
    }

    // Idempotence: Intersect(T, T) = T
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type = *it;
      TypeHandle intersect_type = T.Intersect(type, type);
      CheckEqual(intersect_type, type);
    }

    // Commutativity: Intersect(T1, T2) = Intersect(T2, T1)
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle intersect12 = T.Intersect(type1, type2);
        TypeHandle intersect21 = T.Intersect(type2, type1);
        CheckEqual(intersect12, intersect21);
      }
    }

    // Associativity:
    // Intersect(T1, Intersect(T2, T3)) = Intersect(Intersect(T1, T2), T3)
    // This does NOT hold.  For example:
    // (Class(..stringy1..) /\ Class(..stringy2..)) /\ Constant(..string..) =
    // None
    // Class(..stringy1..) /\ (Class(..stringy2..) /\ Constant(..string..)) =
    // Constant(..string..)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle intersect12 = T.Intersect(type1, type2);
          TypeHandle intersect23 = T.Intersect(type2, type3);
          TypeHandle intersect1_23 = T.Intersect(type1, intersect23);
          TypeHandle intersect12_3 = T.Intersect(intersect12, type3);
          CheckEqual(intersect1_23, intersect12_3);
        }
      }
    }
    */

    // Join: Intersect(T1, T2)->Is(T1) and Intersect(T1, T2)->Is(T2)
    // This does NOT hold.  For example:
    // Class(..stringy..) /\ Constant(..string..) = Constant(..string..)
    // Currently, not even the disjunction holds:
    // Class(Internal/TaggedPtr) /\ (Any/Untagged \/ Context(..)) =
    // Class(Internal/TaggedPtr) \/ Context(..)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle intersect12 = T.Intersect(type1, type2);
        CHECK(intersect12->Is(type1));
        CHECK(intersect12->Is(type2));
      }
    }
    */

    // Lower Boundedness: T1->Is(T2) implies Intersect(T1, T2) = T1
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        TypeHandle intersect12 = T.Intersect(type1, type2);
        if (type1->Is(type2)) CheckEqual(intersect12, type1);
      }
    }

    // Monotonicity: T1->Is(T2) implies Intersect(T1, T3)->Is(Intersect(T2, T3))
    // This does NOT hold.  For example:
    // Class(OtherObject/TaggedPtr) <= Any/TaggedPtr
    // Class(OtherObject/TaggedPtr) /\ Any/UntaggedInt1 = Class(..)
    // Any/TaggedPtr /\ Any/UntaggedInt1 = None
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle intersect13 = T.Intersect(type1, type3);
          TypeHandle intersect23 = T.Intersect(type2, type3);
          CHECK(!type1->Is(type2) || intersect13->Is(intersect23));
        }
      }
    }
    */

    // Monotonicity: T1->Is(T3) or T2->Is(T3) implies Intersect(T1, T2)->Is(T3)
    // This does NOT hold.  For example:
    // Class(..stringy..) <= Class(..stringy..)
    // Class(..stringy..) /\ Constant(..string..) = Constant(..string..)
    // Constant(..string..) </= Class(..stringy..)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle intersect12 = T.Intersect(type1, type2);
          CHECK(!(type1->Is(type3) || type2->Is(type3)) ||
                intersect12->Is(type3));
        }
      }
    }
    */

    // Monotonicity: T1->Is(T2) and T1->Is(T3) implies T1->Is(Intersect(T2, T3))
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle intersect23 = T.Intersect(type2, type3);
          CHECK(!(type1->Is(type2) && type1->Is(type3)) ||
                type1->Is(intersect23));
        }
      }
    }

    // Bitset-class
    CheckEqual(T.Intersect(T.ObjectClass, T.Object), T.ObjectClass);
    CheckEqual(T.Semantic(T.Intersect(T.ObjectClass, T.Number)), T.None);

    // Bitset-array
    CheckEqual(T.Intersect(T.NumberArray, T.Object), T.NumberArray);
    CheckEqual(T.Semantic(T.Intersect(T.AnyArray, T.Proxy)), T.None);

    // Bitset-function
    CheckEqual(T.Intersect(T.MethodFunction, T.Object), T.MethodFunction);
    CheckEqual(T.Semantic(T.Intersect(T.NumberFunction1, T.Proxy)), T.None);

    // Bitset-union
    CheckEqual(
        T.Intersect(T.Object, T.Union(T.ObjectConstant1, T.ObjectClass)),
        T.Union(T.ObjectConstant1, T.ObjectClass));
    CheckEqual(T.Semantic(T.Intersect(T.Union(T.ArrayClass, T.ObjectConstant1),
                                      T.Number)),
               T.None);

    // Class-constant
    CHECK(T.Intersect(T.ObjectConstant1, T.ObjectClass)->IsInhabited());  // !!!
    CHECK(T.Intersect(T.ArrayClass, T.ObjectConstant2)->IsInhabited());

    // Array-union
    CheckEqual(
        T.Intersect(T.NumberArray, T.Union(T.NumberArray, T.ArrayClass)),
        T.NumberArray);
    CheckEqual(
        T.Intersect(T.AnyArray, T.Union(T.Object, T.SmiConstant)),
        T.AnyArray);
    CHECK(
        !T.Intersect(T.Union(T.AnyArray, T.ArrayConstant), T.NumberArray)
            ->IsInhabited());

    // Function-union
    CheckEqual(
        T.Intersect(T.MethodFunction, T.Union(T.String, T.MethodFunction)),
        T.MethodFunction);
    CheckEqual(
        T.Intersect(T.NumberFunction1, T.Union(T.Object, T.SmiConstant)),
        T.NumberFunction1);
    CHECK(
        !T.Intersect(T.Union(T.MethodFunction, T.Name), T.NumberFunction2)
            ->IsInhabited());

    // Class-union
    CheckEqual(
        T.Intersect(T.ArrayClass, T.Union(T.ObjectConstant2, T.ArrayClass)),
        T.ArrayClass);
    CheckEqual(
        T.Intersect(T.ArrayClass, T.Union(T.Object, T.SmiConstant)),
        T.ArrayClass);
    CHECK(
        T.Intersect(T.Union(T.ObjectClass, T.ArrayConstant), T.ArrayClass)
            ->IsInhabited());  // !!!

    // Constant-union
    CheckEqual(
        T.Intersect(
            T.ObjectConstant1, T.Union(T.ObjectConstant1, T.ObjectConstant2)),
        T.ObjectConstant1);
    CheckEqual(
        T.Intersect(T.SmiConstant, T.Union(T.Number, T.ObjectConstant2)),
        T.SmiConstant);
    CHECK(
        T.Intersect(
            T.Union(T.ArrayConstant, T.ObjectClass), T.ObjectConstant1)
                ->IsInhabited());  // !!!

    // Union-union
    CheckEqual(T.Intersect(T.Union(T.Number, T.ArrayClass),
                           T.Union(T.SignedSmall, T.Receiver)),
               T.Union(T.SignedSmall, T.ArrayClass));
    CheckEqual(T.Intersect(T.Union(T.Number, T.ObjectClass),
                           T.Union(T.Signed32, T.OtherObject)),
               T.Union(T.Signed32, T.ObjectClass));
    CheckEqual(
        T.Intersect(
            T.Union(T.ObjectConstant2, T.ObjectConstant1),
            T.Union(T.ObjectConstant1, T.ObjectConstant2)),
        T.Union(T.ObjectConstant2, T.ObjectConstant1));
    CheckEqual(
        T.Intersect(
            T.Union(
                T.ArrayClass,
                T.Union(T.ObjectConstant2, T.ObjectConstant1)),
            T.Union(
                T.ObjectConstant1,
                T.Union(T.ArrayConstant, T.ObjectConstant2))),
        T.Union(
            T.ArrayConstant,
            T.Union(T.ObjectConstant2, T.ObjectConstant1)));  // !!!
  }

  void Distributivity() {
    // Union(T1, Intersect(T2, T3)) = Intersect(Union(T1, T2), Union(T1, T3))
    // This does NOT hold.  For example:
    // Untagged \/ (Untagged /\ Class(../Tagged)) = Untagged \/ Class(../Tagged)
    // (Untagged \/ Untagged) /\ (Untagged \/ Class(../Tagged)) =
    // Untagged /\ (Untagged \/ Class(../Tagged)) = Untagged
    // because Untagged <= Untagged \/ Class(../Tagged)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle union12 = T.Union(type1, type2);
          TypeHandle union13 = T.Union(type1, type3);
          TypeHandle intersect23 = T.Intersect(type2, type3);
          TypeHandle union1_23 = T.Union(type1, intersect23);
          TypeHandle intersect12_13 = T.Intersect(union12, union13);
          CHECK(Equal(union1_23, intersect12_13));
        }
      }
    }
    */

    // Intersect(T1, Union(T2, T3)) = Union(Intersect(T1, T2), Intersect(T1,T3))
    // This does NOT hold.  For example:
    // Untagged /\ (Untagged \/ Class(../Tagged)) = Untagged
    // (Untagged /\ Untagged) \/ (Untagged /\ Class(../Tagged)) =
    // Untagged \/ Class(../Tagged)
    /*
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        for (TypeIterator it3 = T.types.begin(); it3 != T.types.end(); ++it3) {
          TypeHandle type1 = *it1;
          TypeHandle type2 = *it2;
          TypeHandle type3 = *it3;
          TypeHandle intersect12 = T.Intersect(type1, type2);
          TypeHandle intersect13 = T.Intersect(type1, type3);
          TypeHandle union23 = T.Union(type2, type3);
          TypeHandle intersect1_23 = T.Intersect(type1, union23);
          TypeHandle union12_13 = T.Union(intersect12, intersect13);
          CHECK(Equal(intersect1_23, union12_13));
        }
      }
    }
    */
  }

  void GetRange() {
    // GetRange(Range(a, b)) = Range(a, b).
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      TypeHandle type1 = *it1;
      if (type1->IsRange()) {
        typename Type::RangeType* range = type1->GetRange();
        CHECK(type1->Min() == range->Min());
        CHECK(type1->Max() == range->Max());
      }
    }

    // GetRange(Union(Constant(x), Range(min,max))) == Range(min, max).
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        if (type1->IsConstant() && type2->IsRange()) {
          TypeHandle u = T.Union(type1, type2);

          CHECK(type2->Min() == u->GetRange()->Min());
          CHECK(type2->Max() == u->GetRange()->Max());
        }
      }
    }
  }

  template<class Type2, class TypeHandle2, class Region2, class Rep2>
  void Convert() {
    Types<Type2, TypeHandle2, Region2> T2(Rep2::ToRegion(&zone, isolate),
                                          isolate,
                                          isolate->random_number_generator());
    for (TypeIterator it = T.types.begin(); it != T.types.end(); ++it) {
      TypeHandle type1 = *it;
      TypeHandle2 type2 = T2.template Convert<Type>(type1);
      TypeHandle type3 = T.template Convert<Type2>(type2);
      CheckEqual(type1, type3);
    }
  }

  void HTypeFromType() {
    for (TypeIterator it1 = T.types.begin(); it1 != T.types.end(); ++it1) {
      for (TypeIterator it2 = T.types.begin(); it2 != T.types.end(); ++it2) {
        TypeHandle type1 = *it1;
        TypeHandle type2 = *it2;
        HType htype1 = HType::FromType<Type>(type1);
        HType htype2 = HType::FromType<Type>(type2);
        CHECK(!type1->Is(type2) || htype1.IsSubtypeOf(htype2));
      }
    }
  }

  void GlobalObjectType() {
    i::Handle<i::Context> context1 = v8::Utils::OpenHandle(
        *v8::Context::New(reinterpret_cast<v8::Isolate*>(isolate)));
    Handle<i::GlobalObject> global_object1(context1->global_object());
    TypeHandle GlobalObjectConstant1 =
        Type::Constant(global_object1, Rep::ToRegion(&zone, isolate));

    i::Handle<i::Context> context2 = v8::Utils::OpenHandle(
        *v8::Context::New(reinterpret_cast<v8::Isolate*>(isolate)));
    Handle<i::GlobalObject> global_object2(context2->global_object());
    TypeHandle GlobalObjectConstant2 =
        Type::Constant(global_object2, Rep::ToRegion(&zone, isolate));

    CheckSub(GlobalObjectConstant1, T.DetectableObject);
    CheckSub(GlobalObjectConstant2, T.DetectableObject);
    CheckSub(GlobalObjectConstant1, T.GlobalObject);
    CheckSub(GlobalObjectConstant2, T.GlobalObject);
    CheckSub(GlobalObjectConstant1, T.Object);
    CheckSub(GlobalObjectConstant2, T.Object);

    CheckUnordered(T.GlobalObject, T.OtherObject);
    CheckUnordered(GlobalObjectConstant1, T.OtherObject);
    CheckUnordered(GlobalObjectConstant2, T.OtherObject);
    CheckUnordered(GlobalObjectConstant1, GlobalObjectConstant2);

    CheckDisjoint(T.GlobalObject, T.ObjectClass);
    CheckDisjoint(GlobalObjectConstant1, T.ObjectClass);
    CheckDisjoint(GlobalObjectConstant2, T.ArrayClass);

    CheckUnordered(T.Union(T.ObjectClass, T.ArrayClass), T.GlobalObject);
    CheckUnordered(T.Union(T.ObjectClass, T.ArrayClass), GlobalObjectConstant1);
    CheckUnordered(T.Union(T.ObjectClass, T.ArrayClass), GlobalObjectConstant2);

    CheckUnordered(T.Union(T.ObjectConstant1, T.ArrayClass), T.GlobalObject);
    CheckUnordered(T.Union(T.ObjectConstant1, T.ArrayClass),
                   GlobalObjectConstant1);
    CheckUnordered(T.Union(T.ObjectConstant1, T.ArrayClass),
                   GlobalObjectConstant2);

    CheckUnordered(T.Union(T.ObjectClass, T.String), T.GlobalObject);

    CheckSub(T.Union(T.ObjectConstant1, T.ArrayClass),
             T.Union(T.GlobalObject, T.Object));

    CheckDisjoint(T.Union(GlobalObjectConstant1, T.ArrayClass),
                  GlobalObjectConstant2);

    CheckEqual(T.Union(T.Union(T.Number, GlobalObjectConstant1),
                       T.Union(T.SignedSmall, T.GlobalObject)),
               T.Union(T.Number, T.GlobalObject));

    CheckEqual(T.Semantic(T.Intersect(T.ObjectClass, T.GlobalObject)), T.None);

    CHECK(!T.Intersect(T.ArrayClass, GlobalObjectConstant2)->IsInhabited());

    CheckEqual(T.Intersect(T.Union(T.Number, T.OtherObject),
                           T.Union(T.Signed32, T.GlobalObject)),
               T.Signed32);
  }
};

typedef Tests<Type, Type*, Zone, ZoneRep> ZoneTests;
typedef Tests<HeapType, Handle<HeapType>, Isolate, HeapRep> HeapTests;


TEST(IsSomeType_zone) { ZoneTests().IsSomeType(); }


TEST(IsSomeType_heap) { HeapTests().IsSomeType(); }


TEST(PointwiseRepresentation_zone) { ZoneTests().PointwiseRepresentation(); }


TEST(PointwiseRepresentation_heap) { HeapTests().PointwiseRepresentation(); }


TEST(BitsetType_zone) { ZoneTests().Bitset(); }


TEST(BitsetType_heap) { HeapTests().Bitset(); }


TEST(ClassType_zone) { ZoneTests().Class(); }


TEST(ClassType_heap) { HeapTests().Class(); }


TEST(ConstantType_zone) { ZoneTests().Constant(); }


TEST(ConstantType_heap) { HeapTests().Constant(); }


TEST(RangeType_zone) { ZoneTests().Range(); }


TEST(RangeType_heap) { HeapTests().Range(); }


TEST(ArrayType_zone) { ZoneTests().Array(); }


TEST(ArrayType_heap) { HeapTests().Array(); }


TEST(FunctionType_zone) { ZoneTests().Function(); }


TEST(FunctionType_heap) { HeapTests().Function(); }


TEST(Of_zone) { ZoneTests().Of(); }


TEST(Of_heap) { HeapTests().Of(); }


TEST(NowOf_zone) { ZoneTests().NowOf(); }


TEST(NowOf_heap) { HeapTests().NowOf(); }


TEST(MinMax_zone) { ZoneTests().MinMax(); }


TEST(MinMax_heap) { HeapTests().MinMax(); }


TEST(BitsetGlb_zone) { ZoneTests().BitsetGlb(); }


TEST(BitsetGlb_heap) { HeapTests().BitsetGlb(); }


TEST(BitsetLub_zone) { ZoneTests().BitsetLub(); }


TEST(BitsetLub_heap) { HeapTests().BitsetLub(); }


TEST(Is1_zone) { ZoneTests().Is1(); }


TEST(Is1_heap) { HeapTests().Is1(); }


TEST(Is2_zone) { ZoneTests().Is2(); }


TEST(Is2_heap) { HeapTests().Is2(); }


TEST(NowIs_zone) { ZoneTests().NowIs(); }


TEST(NowIs_heap) { HeapTests().NowIs(); }


TEST(Contains_zone) { ZoneTests().Contains(); }


TEST(Contains_heap) { HeapTests().Contains(); }


TEST(NowContains_zone) { ZoneTests().NowContains(); }


TEST(NowContains_heap) { HeapTests().NowContains(); }


TEST(Maybe_zone) { ZoneTests().Maybe(); }


TEST(Maybe_heap) { HeapTests().Maybe(); }


TEST(Union1_zone) { ZoneTests().Union1(); }


TEST(Union1_heap) { HeapTests().Union1(); }


TEST(Union2_zone) { ZoneTests().Union2(); }


TEST(Union2_heap) { HeapTests().Union2(); }


TEST(Union3_zone) { ZoneTests().Union3(); }


TEST(Union3_heap) { HeapTests().Union3(); }


TEST(Union4_zone) { ZoneTests().Union4(); }


TEST(Union4_heap) { HeapTests().Union4(); }


TEST(Intersect_zone) { ZoneTests().Intersect(); }


TEST(Intersect_heap) { HeapTests().Intersect(); }


TEST(Distributivity_zone) { ZoneTests().Distributivity(); }


TEST(Distributivity_heap) { HeapTests().Distributivity(); }


TEST(GetRange_zone) { ZoneTests().GetRange(); }


TEST(GetRange_heap) { HeapTests().GetRange(); }


TEST(Convert_zone) {
  ZoneTests().Convert<HeapType, Handle<HeapType>, Isolate, HeapRep>();
}


TEST(Convert_heap) { HeapTests().Convert<Type, Type*, Zone, ZoneRep>(); }


TEST(HTypeFromType_zone) { ZoneTests().HTypeFromType(); }


TEST(HTypeFromType_heap) { HeapTests().HTypeFromType(); }


TEST(GlobalObjectType_zone) { ZoneTests().GlobalObjectType(); }


TEST(GlobalObjectType_heap) { HeapTests().GlobalObjectType(); }
