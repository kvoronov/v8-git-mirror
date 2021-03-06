// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/objects.h"
#include "src/transitions-inl.h"
#include "src/utils.h"

namespace v8 {
namespace internal {


Handle<TransitionArray> TransitionArray::Allocate(Isolate* isolate,
                                                  int number_of_transitions,
                                                  int slack) {
  Handle<FixedArray> array = isolate->factory()->NewFixedArray(
      LengthFor(number_of_transitions + slack));
  array->set(kPrototypeTransitionsIndex, Smi::FromInt(0));
  array->set(kTransitionLengthIndex, Smi::FromInt(number_of_transitions));
  return Handle<TransitionArray>::cast(array);
}


Handle<TransitionArray> TransitionArray::AllocateSimple(Isolate* isolate,
                                                        Handle<Map> target) {
  Handle<FixedArray> array =
      isolate->factory()->NewFixedArray(kSimpleTransitionSize);
  array->set(kSimpleTransitionTarget, *target);
  return Handle<TransitionArray>::cast(array);
}


void TransitionArray::NoIncrementalWriteBarrierCopyFrom(TransitionArray* origin,
                                                        int origin_transition,
                                                        int target_transition) {
  NoIncrementalWriteBarrierSet(target_transition,
                               origin->GetKey(origin_transition),
                               origin->GetTarget(origin_transition));
}


static bool InsertionPointFound(Name* key1, Name* key2) {
  return key1->Hash() > key2->Hash();
}


Handle<TransitionArray> TransitionArray::NewWith(Handle<Map> map,
                                                 Handle<Name> name,
                                                 Handle<Map> target,
                                                 SimpleTransitionFlag flag) {
  Handle<TransitionArray> result;
  Isolate* isolate = name->GetIsolate();

  if (flag == SIMPLE_TRANSITION) {
    result = AllocateSimple(isolate, target);
  } else {
    result = Allocate(isolate, 1);
    result->NoIncrementalWriteBarrierSet(0, *name, *target);
  }
  result->set_back_pointer_storage(map->GetBackPointer());
  return result;
}


Handle<TransitionArray> TransitionArray::ExtendToFullTransitionArray(
    Handle<Map> containing_map) {
  DCHECK(!containing_map->transitions()->IsFullTransitionArray());
  int nof = containing_map->transitions()->number_of_transitions();

  // A transition array may shrink during GC.
  Handle<TransitionArray> result = Allocate(containing_map->GetIsolate(), nof);
  DisallowHeapAllocation no_gc;
  int new_nof = containing_map->transitions()->number_of_transitions();
  if (new_nof != nof) {
    DCHECK(new_nof == 0);
    result->Shrink(ToKeyIndex(0));
    result->SetNumberOfTransitions(0);
  } else if (nof == 1) {
    result->NoIncrementalWriteBarrierCopyFrom(
        containing_map->transitions(), kSimpleTransitionIndex, 0);
  }

  result->set_back_pointer_storage(
      containing_map->transitions()->back_pointer_storage());
  return result;
}


Handle<TransitionArray> TransitionArray::Insert(Handle<Map> map,
                                                Handle<Name> name,
                                                Handle<Map> target,
                                                SimpleTransitionFlag flag) {
  if (!map->HasTransitionArray()) {
    return TransitionArray::NewWith(map, name, target, flag);
  }

  int number_of_transitions = map->transitions()->number_of_transitions();
  int new_nof = number_of_transitions;

  int insertion_index = map->transitions()->Search(*name);
  if (insertion_index == kNotFound) ++new_nof;
  CHECK(new_nof <= kMaxNumberOfTransitions);

  if (new_nof <= map->transitions()->number_of_transitions_storage()) {
    DisallowHeapAllocation no_gc;
    TransitionArray* array = map->transitions();

    if (insertion_index != kNotFound) {
      array->SetTarget(insertion_index, *target);
      return handle(array);
    }

    array->SetNumberOfTransitions(new_nof);
    uint32_t hash = name->Hash();
    for (insertion_index = number_of_transitions; insertion_index > 0;
         --insertion_index) {
      Name* key = array->GetKey(insertion_index - 1);
      if (key->Hash() <= hash) break;
      array->SetKey(insertion_index, key);
      array->SetTarget(insertion_index, array->GetTarget(insertion_index - 1));
    }
    array->SetKey(insertion_index, *name);
    array->SetTarget(insertion_index, *target);
    return handle(array);
  }

  Handle<TransitionArray> result = Allocate(
      map->GetIsolate(), new_nof,
      Map::SlackForArraySize(number_of_transitions, kMaxNumberOfTransitions));

  // The map's transition array may grown smaller during the allocation above as
  // it was weakly traversed, though it is guaranteed not to disappear. Trim the
  // result copy if needed, and recompute variables.
  DCHECK(map->HasTransitionArray());
  DisallowHeapAllocation no_gc;
  TransitionArray* array = map->transitions();
  if (array->number_of_transitions() != number_of_transitions) {
    DCHECK(array->number_of_transitions() < number_of_transitions);

    number_of_transitions = array->number_of_transitions();
    new_nof = number_of_transitions;

    insertion_index = array->Search(*name);
    if (insertion_index == kNotFound) ++new_nof;

    result->Shrink(ToKeyIndex(new_nof));
    result->SetNumberOfTransitions(new_nof);
  }

  if (array->HasPrototypeTransitions()) {
    result->SetPrototypeTransitions(array->GetPrototypeTransitions());
  }

  insertion_index = 0;
  for (; insertion_index < number_of_transitions; ++insertion_index) {
    if (InsertionPointFound(array->GetKey(insertion_index), *name)) break;
    result->NoIncrementalWriteBarrierCopyFrom(
        array, insertion_index, insertion_index);
  }

  result->NoIncrementalWriteBarrierSet(insertion_index, *name, *target);

  for (; insertion_index < number_of_transitions; ++insertion_index) {
    result->NoIncrementalWriteBarrierCopyFrom(
        array, insertion_index, insertion_index + 1);
  }

  result->set_back_pointer_storage(array->back_pointer_storage());
  return result;
}


} }  // namespace v8::internal
