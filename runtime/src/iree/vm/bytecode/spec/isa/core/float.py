# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Selected-width IEEE floating-point instructions."""

from __future__ import annotations

import enum
from typing import NamedTuple

from iree.vm.bytecode.spec.isa import (
    FieldRole,
    FieldRuleUse,
    Instruction,
    InstructionFamily,
    InstructionField,
)
from iree.vm.bytecode.spec.isa.core.rules import FieldRule
from iree.vm.bytecode.spec.schema import (
    U8,
    U16,
    Field,
    NumericKind,
    NumericTable,
    NumericValue,
)
from iree.vm.bytecode.spec.version import CORE_0


def _selector(
    name: str, summary: str, values: tuple[tuple[str, int, str], ...]
) -> NumericTable:
    return NumericTable(
        name,
        U8,
        NumericKind.SELECTOR,
        tuple(
            NumericValue(value_name, value, CORE_0, meaning)
            for value_name, value, meaning in values
        ),
        CORE_0,
        summary,
    )


FLOAT_MINMAX_SELECTOR = _selector(
    "float.minmax",
    (
        "Selects IEEE minimum/maximum or number-selecting minnum/maxnum. "
        "Numeric ordering governs ordinary values; minima choose -0 and maxima "
        "choose +0 from opposite signed zeros."
    ),
    (
        (
            "minimum",
            0,
            "Returns the numeric minimum and an arithmetic NaN if either operand "
            "is NaN.",
        ),
        (
            "maximum",
            1,
            "Returns the numeric maximum and an arithmetic NaN if either operand "
            "is NaN.",
        ),
        (
            "minnum",
            2,
            "Returns the sole numeric operand bit-for-bit, or an arithmetic NaN "
            "when both are NaN.",
        ),
        (
            "maxnum",
            3,
            "Returns the sole numeric operand bit-for-bit, or an arithmetic NaN "
            "when both are NaN.",
        ),
    ),
)

_FLOAT_MATH_EXACT_UNARY_VALUES = (
    (
        "ceil",
        0,
        "Returns the integral value toward positive infinity while preserving "
        "signed zero and signed infinity.",
    ),
    (
        "floor",
        1,
        "Returns the integral value toward negative infinity while preserving "
        "signed zero and signed infinity.",
    ),
    (
        "round_even",
        2,
        "Returns the nearest integral value with halfway cases to even, preserving "
        "signed zero and signed infinity.",
    ),
    (
        "trunc",
        3,
        "Returns the integral value toward zero while preserving signed zero and "
        "signed infinity.",
    ),
    (
        "sign",
        4,
        "Classifies raw bits without arithmetic: NaNs and either zero become +0; "
        "negative nonzero values, including -infinity, become -1; positive nonzero "
        "values, including +infinity, become +1.",
    ),
)

FLOAT_MATH_F32_SELECTOR = _selector(
    "float.math.f32",
    (
        "Selects one frozen unary f32 mapping. Exact operations are shared with "
        "f64 by ordinal; approximation selectors name complete result mappings, "
        "not an accuracy mode."
    ),
    _FLOAT_MATH_EXACT_UNARY_VALUES
    + (
        (
            "exp2.approx",
            5,
            "Returns the frozen machine-like 2^x mapping. Finite normal-path "
            "results are within one adjacent f32 value of correct rounding; values "
            "below -126 flush to +0, values at least 128 become +infinity, and "
            "every NaN becomes the canonical positive quiet NaN.",
        ),
        (
            "log2.approx",
            6,
            "Returns the frozen machine-like log2(x) mapping. Zeros and subnormals "
            "become -infinity; negative normal values and every NaN become the "
            "canonical positive quiet NaN; positive infinity is preserved; finite "
            "normal-path results are within one adjacent f32 value of correct "
            "rounding.",
        ),
        (
            "reciprocal.approx",
            7,
            "Returns the frozen machine-like 1/x mapping. Zero and subnormal inputs "
            "become signed infinity, infinities become signed zero, subnormal "
            "results flush to signed zero, and every NaN becomes the canonical "
            "positive quiet NaN.",
        ),
        (
            "rsqrt.approx",
            8,
            "Returns the frozen machine-like 1/sqrt(x) mapping with separate f32 "
            "rounding after square root and division. Signed zero and subnormal "
            "inputs become signed infinity, positive infinity becomes +0, every "
            "other negative input and every NaN become the canonical positive quiet "
            "NaN, and finite results are within one adjacent f32 value of correct "
            "rounding.",
        ),
        (
            "sqrt.approx",
            9,
            "Returns the frozen machine-like sqrt(x) mapping. Signed zero and "
            "subnormal inputs become signed zero, positive infinity is preserved, "
            "and every other negative input and every NaN become the canonical "
            "positive quiet NaN.",
        ),
        (
            "sin_turns.approx",
            10,
            "Returns the correctly rounded frozen sin(2*pi*x) mapping for every f32 "
            "payload. Cardinal results are structural and every nonfinite input "
            "becomes the canonical positive quiet NaN.",
        ),
        (
            "cos_turns.approx",
            11,
            "Returns the correctly rounded frozen cos(2*pi*x) mapping for every f32 "
            "payload. Cardinal results are structural and every nonfinite input "
            "becomes the canonical positive quiet NaN.",
        ),
    ),
)

FLOAT_MATH_F64_SELECTOR = _selector(
    "float.math.f64",
    (
        "Selects one exact unary f64 mapping. Version zero has no f64 approximate "
        "machine leaves."
    ),
    _FLOAT_MATH_EXACT_UNARY_VALUES,
)

FLOAT_COMPARE_SELECTOR = _selector(
    "float.compare",
    (
        "Selects an ordered or unordered IEEE predicate. Ordered predicates "
        "require both operands to be non-NaN; unordered predicates are true when "
        "either operand is NaN. Signed zeros compare equal."
    ),
    (
        ("oeq", 0, "True when neither operand is NaN and left equals right."),
        (
            "ogt",
            1,
            "True when neither operand is NaN and left is greater than right.",
        ),
        (
            "oge",
            2,
            "True when neither operand is NaN and left is at least right.",
        ),
        (
            "olt",
            3,
            "True when neither operand is NaN and left is less than right.",
        ),
        (
            "ole",
            4,
            "True when neither operand is NaN and left is at most right.",
        ),
        (
            "one",
            5,
            "True when neither operand is NaN and left differs from right.",
        ),
        ("ord", 6, "True when neither operand is NaN."),
        ("ueq", 7, "True when either operand is NaN or left equals right."),
        (
            "ugt",
            8,
            "True when either operand is NaN or left is greater than right.",
        ),
        (
            "uge",
            9,
            "True when either operand is NaN or left is at least right.",
        ),
        (
            "ult",
            10,
            "True when either operand is NaN or left is less than right.",
        ),
        (
            "ule",
            11,
            "True when either operand is NaN or left is at most right.",
        ),
        (
            "une",
            12,
            "True when either operand is NaN or left differs from right.",
        ),
        ("uno", 13, "True when either operand is NaN."),
    ),
)

FLOAT_CLASSIFY_SELECTOR = _selector(
    "float.classify",
    "Selects a raw exponent/significand classification without floating arithmetic.",
    (
        ("isnan", 0, "True for quiet or signaling NaN payloads."),
        ("isinf", 1, "True for either signed infinity and false for NaNs."),
        (
            "isfinite",
            2,
            "True for zero, subnormal, and normal finite payloads.",
        ),
    ),
)

FLOAT_CLAMP_SELECTOR = _selector(
    "float.clamp",
    (
        "Selects one exact selected-width clamp composition. All modes are "
        "defined when lower exceeds upper."
    ),
    (
        (
            "ordered",
            0,
            "Starts with value, selects lower when result<lower, then upper when "
            "result>upper; NaN comparisons are false.",
        ),
        (
            "number",
            1,
            "Computes minnum(maxnum(value, lower), upper).",
        ),
        (
            "ieee",
            2,
            "Computes minimum(maximum(value, lower), upper).",
        ),
    ),
)

FLOAT_SELECTORS = (
    FLOAT_MINMAX_SELECTOR,
    FLOAT_MATH_F32_SELECTOR,
    FLOAT_MATH_F64_SELECTOR,
    FLOAT_COMPARE_SELECTOR,
    FLOAT_CLASSIFY_SELECTOR,
    FLOAT_CLAMP_SELECTOR,
)


FLOAT_FAMILY = InstructionFamily(
    name="float",
    since=CORE_0,
    summary="Selected-width IEEE floating arithmetic and math operations.",
    contract=(
        "f32 and f64 are IEEE 754 binary32 and binary64 bit patterns. f32 reads "
        "low 32 cell bits and clears every high result bit; f64 consumes and "
        "produces the complete cell. Each arithmetic record executes and rounds "
        "independently at its selected width using nearest-even, without f32 "
        "promotion through f64 or retained excess precision. Subnormals use gradual "
        "underflow and contraction occurs only in explicit float.fma records. Each "
        "approximation selector owns its documented denormal policy instead of "
        "inheriting this default.\n\nBefore "
        "every start or resume drive segment, the runtime saves the calling thread's "
        "floating environment and installs masked exceptions, nearest-even rounding, "
        "and preserved input/output subnormals. Every segment exit restores the saved "
        "environment, including completion, suspension, and failure. Nested VM calls "
        "inherit the installed profile. Synchronous provider phases invoked by the "
        "drive execute under and must preserve it; asynchronous wake and completion "
        "callbacks execute no VM numeric work and inherit no VM floating contract. A "
        "host unable to establish this profile fails before bytecode executes.\n\n"
        "Floating flags and errno are unobservable, and domains, poles, "
        "division by zero, overflow, underflow, and invalid arithmetic produce "
        "floating results, never VM status failures. An arithmetic NaN has an all-one "
        "exponent, nonzero significand, and quiet bit set. Its sign is unspecified. An "
        "invalid operation with no NaN input, or an operation whose every NaN input "
        "has canonical payload 0x7FC00000 for f32 or 0x7FF8000000000000 for f64 "
        "when input signs are ignored, produces that canonical payload. An operation "
        "with a noncanonical NaN input may produce any arithmetic NaN payload.\n\n"
        "Neg, abs, copysign, comparisons, classification, sign, and ordered clamp "
        "use raw payload logic where stated and never accidentally quiet or trap on "
        "signaling payloads. Every named approximate f32 selector freezes one complete "
        "result mapping, including NaN payload and denormal behavior, and never calls "
        "host libm. The selector suffix is part of the operation identity; it does not "
        "permit any result inside an error interval. Exact integral rounding and fma "
        "have one selected-width mapping. No instruction carries an ambient fast-math "
        "or approximation mode. Compound functions and strict library roots are "
        "ordinary compiler recipes or callable libraries, not Core selectors.\n\n"
        "All operations read inputs before destination publication, are infallible "
        "after verification, access no refs, and never suspend."
    ),
)


def _field(
    name: str,
    encoding,
    summary: str,
    role: FieldRole,
    rule,
    *,
    element_count: int = 1,
) -> InstructionField:
    if not isinstance(rule, FieldRuleUse):
        rule = FieldRuleUse(rule)
    return InstructionField(Field(name, encoding, summary, element_count), role, rule)


def _value(name: str, role: FieldRole, summary: str) -> InstructionField:
    return _field(name, U8, summary, role, FieldRule.REGISTER_VALUE)


def _selected_value(
    name: str,
    role: FieldRole,
    summary: str,
    table: NumericTable,
) -> InstructionField:
    return _field(name, U8, summary, role, FieldRuleUse(FieldRule.SELECTOR, data=table))


def _padding(encoding=U8, *, element_count: int = 1) -> InstructionField:
    return _field(
        f"zero_padding_{encoding.name}",
        encoding,
        "Canonical zero padding.",
        FieldRole.PADDING,
        FieldRule.ZERO,
        element_count=element_count,
    )


def _result(width: int) -> str:
    if width == 32:
        return "destination_v8 receives the f32 result and clears its high cell half."
    return "destination_v8 receives the complete 64-bit f64 result."


class _FloatDataPath(enum.Enum):
    ARITHMETIC = "arithmetic"
    RAW_BITS = "raw_bits"


class _BinaryDefinition(NamedTuple):
    opcode: int
    mnemonic: str
    summary: str
    expression: str
    data_path: _FloatDataPath = _FloatDataPath.ARITHMETIC


def _binary(definition: _BinaryDefinition) -> Instruction:
    opcode, mnemonic, summary, expression, data_path = definition
    width = 32 if mnemonic.endswith("32") else 64
    read = "read_float_bits" if data_path == _FloatDataPath.RAW_BITS else "read_float"
    write = (
        "write_float_bits" if data_path == _FloatDataPath.RAW_BITS else "write_float"
    )
    return Instruction(
        opcode=opcode,
        mnemonic=mnemonic,
        since=CORE_0,
        family=FLOAT_FAMILY,
        summary=summary,
        fields=(
            _value(
                "destination_v8",
                FieldRole.RESULT,
                "Value-register ordinal receiving the result.",
            ),
            _value("left_v8", FieldRole.OPERAND, "Left value-register ordinal."),
            _value("right_v8", FieldRole.OPERAND, "Right value-register ordinal."),
        ),
        semantics=None,
        behavior=f"Reads both operands before evaluating one {width}-bit result.",
        success=(_result(width),),
        assembly=f"%v<destination> = {mnemonic} %v<left>, %v<right>",
        pseudocode=(
            f"left = {read}(left_v8, {width});\n"
            f"right = {read}(right_v8, {width});\n"
            f"result = {expression};\n"
            f"{write}(destination_v8, result, {width});\n"
            "pc = pc + 4;"
        ),
    )


_BINARY_DEFINITIONS = (
    _BinaryDefinition(
        0x80,
        "float.add.f32",
        "Adds two f32 values with one selected-width rounding.",
        "left + right",
    ),
    _BinaryDefinition(
        0x81,
        "float.add.f64",
        "Adds two f64 values with one selected-width rounding.",
        "left + right",
    ),
    _BinaryDefinition(
        0x82,
        "float.sub.f32",
        "Subtracts two f32 values with one selected-width rounding.",
        "left - right",
    ),
    _BinaryDefinition(
        0x83,
        "float.sub.f64",
        "Subtracts two f64 values with one selected-width rounding.",
        "left - right",
    ),
    _BinaryDefinition(
        0x84,
        "float.mul.f32",
        "Multiplies two f32 values with one selected-width rounding.",
        "left * right",
    ),
    _BinaryDefinition(
        0x85,
        "float.mul.f64",
        "Multiplies two f64 values with one selected-width rounding.",
        "left * right",
    ),
    _BinaryDefinition(
        0x86,
        "float.div.f32",
        "Divides two f32 values with IEEE non-stop semantics.",
        "left / right",
    ),
    _BinaryDefinition(
        0x87,
        "float.div.f64",
        "Divides two f64 values with IEEE non-stop semantics.",
        "left / right",
    ),
    _BinaryDefinition(
        0x88,
        "float.rem.f32",
        "Computes width-matched f32 fmod. A numeric result has the dividend's sign, "
        "including zero; a zero divisor or infinite dividend produces an arithmetic "
        "NaN, while an infinite divisor returns a finite dividend unchanged.",
        "host_math(fmod, left, right, 32)",
    ),
    _BinaryDefinition(
        0x89,
        "float.rem.f64",
        "Computes width-matched f64 fmod. A numeric result has the dividend's sign, "
        "including zero; a zero divisor or infinite dividend produces an arithmetic "
        "NaN, while an infinite divisor returns a finite dividend unchanged.",
        "host_math(fmod, left, right, 64)",
    ),
    _BinaryDefinition(
        0x96,
        "float.copysign.f32",
        "Copies the raw f32 sign while preserving every non-sign payload bit.",
        "(left & 0x7FFFFFFF) | (right & 0x80000000)",
        _FloatDataPath.RAW_BITS,
    ),
    _BinaryDefinition(
        0x97,
        "float.copysign.f64",
        "Copies the raw f64 sign while preserving every non-sign payload bit.",
        "(left & 0x7FFFFFFFFFFFFFFF) | (right & 0x8000000000000000)",
        _FloatDataPath.RAW_BITS,
    ),
)


class _SignDefinition(NamedTuple):
    opcode: int
    mnemonic: str
    summary: str
    expression: str


def _sign_unary(definition: _SignDefinition) -> Instruction:
    opcode, mnemonic, summary, expression = definition
    width = 32 if mnemonic.endswith("32") else 64
    return Instruction(
        opcode=opcode,
        mnemonic=mnemonic,
        since=CORE_0,
        family=FLOAT_FAMILY,
        summary=summary,
        fields=(
            _value(
                "destination_v8",
                FieldRole.RESULT,
                "Value-register ordinal receiving the result.",
            ),
            _value("source_v8", FieldRole.OPERAND, "Source value-register ordinal."),
            _padding(),
        ),
        semantics=None,
        behavior=(
            "Transforms the raw payload without floating arithmetic, quieting NaNs, "
            "or raising a floating exception."
        ),
        success=(
            "destination_v8 receives the exact transformed payload"
            + (" and clears its high cell half." if width == 32 else "."),
        ),
        assembly=f"%v<destination> = {mnemonic} %v<source>",
        pseudocode=(
            f"bits = read_float_bits(source_v8, {width});\n"
            f"write_float_bits(destination_v8, {expression}, {width});\n"
            "pc = pc + 4;"
        ),
    )


_SIGN_DEFINITIONS = (
    _SignDefinition(
        0x8A, "float.neg.f32", "Toggles the raw f32 sign bit.", "bits ^ 0x80000000"
    ),
    _SignDefinition(
        0x8B,
        "float.neg.f64",
        "Toggles the raw f64 sign bit.",
        "bits ^ 0x8000000000000000",
    ),
    _SignDefinition(
        0x8C, "float.abs.f32", "Clears the raw f32 sign bit.", "bits & 0x7FFFFFFF"
    ),
    _SignDefinition(
        0x8D,
        "float.abs.f64",
        "Clears the raw f64 sign bit.",
        "bits & 0x7FFFFFFFFFFFFFFF",
    ),
)


class _SelectedBinaryDefinition(NamedTuple):
    opcode: int
    mnemonic: str
    selector_name: str
    selector: NumericTable
    summary: str
    result: str
    evaluator: str


def _selected_binary(definition: _SelectedBinaryDefinition) -> Instruction:
    opcode, mnemonic, selector_name, selector, summary, result, evaluator = definition
    width = 32 if mnemonic.endswith("32") else 64
    reads = "read_float_bits"
    if selector is FLOAT_COMPARE_SELECTOR:
        publish = "values[destination_v8] = canonical_bool(result);\n"
    else:
        publish = f"write_float_bits(destination_v8, result, {width});\n"
    return Instruction(
        opcode=opcode,
        mnemonic=mnemonic,
        since=CORE_0,
        family=FLOAT_FAMILY,
        summary=summary,
        fields=(
            _value(
                "destination_v8",
                FieldRole.RESULT,
                "Value-register ordinal receiving the result.",
            ),
            _value("left_v8", FieldRole.OPERAND, "Left value-register ordinal."),
            _value("right_v8", FieldRole.OPERAND, "Right value-register ordinal."),
            _selected_value(
                selector_name,
                FieldRole.IMMEDIATE,
                f"Closed {selector.name} operation selector.",
                selector,
            ),
            _padding(element_count=3),
        ),
        semantics=None,
        behavior=(
            f"Evaluates the selected {width}-bit {selector.name} operation under "
            "the selector's exact contract."
        ),
        success=(result,),
        assembly=(
            f"%v<destination> = {mnemonic} %v<left>, %v<right> "
            + ("{predicate}" if selector is FLOAT_COMPARE_SELECTOR else "{selector}")
        ),
        pseudocode=(
            f"left = {reads}(left_v8, {width});\n"
            f"right = {reads}(right_v8, {width});\n"
            f"result = {evaluator}({selector_name}, left, right, {width});\n"
            + publish
            + "pc = pc + 8;"
        ),
    )


_SELECTED_BINARY_DEFINITIONS = (
    _SelectedBinaryDefinition(
        0x8E,
        "float.minmax.f32",
        "selector_u8",
        FLOAT_MINMAX_SELECTOR,
        "Selects f32 IEEE or number-selecting minimum/maximum.",
        (
            "Minimum/maximum propagate an arithmetic NaN from either NaN input; "
            "minnum/maxnum return the sole numeric operand bit-for-bit and propagate "
            "only when both inputs are NaN. Opposite zeros select -0 for minima and "
            "+0 for maxima. The f32 result clears the high cell half."
        ),
        "evaluate_float_minmax",
    ),
    _SelectedBinaryDefinition(
        0x8F,
        "float.minmax.f64",
        "selector_u8",
        FLOAT_MINMAX_SELECTOR,
        "Selects f64 IEEE or number-selecting minimum/maximum.",
        (
            "Minimum/maximum propagate an arithmetic NaN from either NaN input; "
            "minnum/maxnum return the sole numeric operand bit-for-bit and propagate "
            "only when both inputs are NaN. Opposite zeros select -0 for minima and "
            "+0 for maxima."
        ),
        "evaluate_float_minmax",
    ),
    _SelectedBinaryDefinition(
        0x90,
        "float.compare.f32",
        "predicate_u8",
        FLOAT_COMPARE_SELECTOR,
        "Evaluates one ordered or unordered raw-payload f32 predicate.",
        (
            "destination_v8 receives canonical complete-cell zero or one. Either NaN "
            "makes ordered predicates false and unordered predicates true as "
            "selected; signed zeros compare equal."
        ),
        "evaluate_float_predicate",
    ),
    _SelectedBinaryDefinition(
        0x91,
        "float.compare.f64",
        "predicate_u8",
        FLOAT_COMPARE_SELECTOR,
        "Evaluates one ordered or unordered raw-payload f64 predicate.",
        (
            "destination_v8 receives canonical complete-cell zero or one. Either NaN "
            "makes ordered predicates false and unordered predicates true as "
            "selected; signed zeros compare equal."
        ),
        "evaluate_float_predicate",
    ),
)


def _classify(opcode: int, width: int) -> Instruction:
    mnemonic = f"float.classify.f{width}"
    return Instruction(
        opcode=opcode,
        mnemonic=mnemonic,
        since=CORE_0,
        family=FLOAT_FAMILY,
        summary=f"Classifies one raw f{width} payload without arithmetic.",
        fields=(
            _value(
                "destination_v8",
                FieldRole.RESULT,
                "Value-register ordinal receiving the result.",
            ),
            _value("source_v8", FieldRole.OPERAND, "Source value-register ordinal."),
            _selected_value(
                "selector_u8",
                FieldRole.IMMEDIATE,
                "Closed float.classify operation selector.",
                FLOAT_CLASSIFY_SELECTOR,
            ),
        ),
        semantics=None,
        behavior=(
            "Tests raw exponent and significand bits for NaN, infinity, or finite "
            "without performing floating arithmetic."
        ),
        success=(
            "destination_v8 receives canonical complete-cell zero or one without "
            "trapping on a signaling NaN payload.",
        ),
        assembly=f"%v<destination> = {mnemonic} %v<source> {{selector}}",
        pseudocode=(
            f"bits = read_float_bits(source_v8, {width});\n"
            "values[destination_v8] = canonical_bool(\n"
            f"    classify_float(selector_u8, bits, {width}));\n"
            "pc = pc + 4;"
        ),
    )


def _clamp(opcode: int, width: int) -> Instruction:
    mnemonic = f"float.clamp.f{width}"
    return Instruction(
        opcode=opcode,
        mnemonic=mnemonic,
        since=CORE_0,
        family=FLOAT_FAMILY,
        summary=f"Clamps one f{width} payload with explicit NaN policy.",
        fields=(
            _value(
                "destination_v8",
                FieldRole.RESULT,
                "Value-register ordinal receiving the result.",
            ),
            _value("value_v8", FieldRole.OPERAND, "Value to clamp."),
            _value("lower_v8", FieldRole.OPERAND, "Lower-bound payload."),
            _value("upper_v8", FieldRole.OPERAND, "Upper-bound payload."),
            _selected_value(
                "mode_u8",
                FieldRole.IMMEDIATE,
                "Closed float.clamp operation selector.",
                FLOAT_CLAMP_SELECTOR,
            ),
            _padding(U16),
        ),
        semantics=None,
        behavior=(
            "Ordered mode applies two ordered comparisons and selects, preserving a "
            "NaN value bit-for-bit and ignoring a NaN bound. Number mode composes "
            "maxnum then minnum; IEEE mode composes maximum then minimum. Every mode "
            "is total when lower exceeds upper."
        ),
        success=(
            "destination_v8 receives the exact selected-width composition, including "
            "its mode-specific NaN and signed-zero behavior"
            + (" and clears the high cell half." if width == 32 else "."),
        ),
        assembly=(
            f"%v<destination> = {mnemonic} %v<value>, %v<lower>, %v<upper> {{mode}}"
        ),
        pseudocode=(
            f"value = read_float_bits(value_v8, {width});\n"
            f"lower = read_float_bits(lower_v8, {width});\n"
            f"upper = read_float_bits(upper_v8, {width});\n"
            "result = evaluate_float_clamp(\n"
            f"    mode_u8, value, lower, upper, {width});\n"
            f"write_float_bits(destination_v8, result, {width});\n"
            "pc = pc + 8;"
        ),
    )


def _math_unary(opcode: int, width: int) -> Instruction:
    mnemonic = f"float.math.unary.f{width}"
    selector = FLOAT_MATH_F32_SELECTOR if width == 32 else FLOAT_MATH_F64_SELECTOR
    return Instruction(
        opcode=opcode,
        mnemonic=mnemonic,
        since=CORE_0,
        family=FLOAT_FAMILY,
        summary=f"Evaluates one closed unary f{width} math operation.",
        fields=(
            _value(
                "destination_v8",
                FieldRole.RESULT,
                "Value-register ordinal receiving the result.",
            ),
            _value("source_v8", FieldRole.OPERAND, "Source value-register ordinal."),
            _selected_value(
                "selector_u8",
                FieldRole.IMMEDIATE,
                f"Closed {selector.name} operation selector.",
                selector,
            ),
        ),
        semantics=None,
        behavior=(
            "Evaluates the selector's frozen mapping under the family floating "
            "profile. Approximate f32 leaves have selector-specific denormal and NaN "
            "behavior. Structural sign uses raw bits."
        ),
        success=(
            "destination_v8 receives the selector-defined exact result payload"
            + (" and clears the high cell half." if width == 32 else "."),
        ),
        assembly=f"%v<destination> = {mnemonic} %v<source> {{selector}}",
        pseudocode=(
            f"source_bits = read_float_bits(source_v8, {width});\n"
            "result_bits = evaluate_float_math_unary(\n"
            f"    selector_u8, source_bits, {width});\n"
            f"write_float_bits(destination_v8, result_bits, {width});\n"
            "pc = pc + 4;"
        ),
    )


def _fma(opcode: int, width: int) -> Instruction:
    mnemonic = f"float.fma.f{width}"
    return Instruction(
        opcode=opcode,
        mnemonic=mnemonic,
        since=CORE_0,
        family=FLOAT_FAMILY,
        summary=f"Computes one exact fused f{width} multiply-add.",
        fields=(
            _value(
                "destination_v8",
                FieldRole.RESULT,
                "Value-register ordinal receiving the result.",
            ),
            _value("a_v8", FieldRole.OPERAND, "Multiplicand A payload."),
            _value("b_v8", FieldRole.OPERAND, "Multiplicand B payload."),
            _value("c_v8", FieldRole.OPERAND, "Addend C payload."),
            _padding(element_count=3),
        ),
        semantics=None,
        behavior=(
            "Computes infinitely precise a*b+c and rounds once to selected width."
        ),
        success=(
            "destination_v8 receives the IEEE fused-multiply-add result and inherited "
            "arithmetic-NaN behavior after one final rounding"
            + (" and clears the high cell half." if width == 32 else "."),
        ),
        assembly=f"%v<destination> = {mnemonic} %v<a>, %v<b>, %v<c>",
        pseudocode=(
            "result = fused_multiply_add(\n"
            f"    read_float(a_v8, {width}), read_float(b_v8, {width}),\n"
            f"    read_float(c_v8, {width}), {width});\n"
            f"write_float(destination_v8, result, {width});\n"
            "pc = pc + 8;"
        ),
    )


_INSTRUCTIONS_BY_OPCODE = {
    instruction.opcode: instruction
    for instruction in (
        *(_binary(definition) for definition in _BINARY_DEFINITIONS),
        *(_sign_unary(definition) for definition in _SIGN_DEFINITIONS),
        *(_selected_binary(definition) for definition in _SELECTED_BINARY_DEFINITIONS),
        _classify(0x92, 32),
        _classify(0x93, 64),
        _clamp(0x94, 32),
        _clamp(0x95, 64),
        _math_unary(0x98, 32),
        _math_unary(0x99, 64),
        _fma(0x9A, 32),
        _fma(0x9B, 64),
    )
}
FLOAT_INSTRUCTIONS = tuple(
    _INSTRUCTIONS_BY_OPCODE[opcode] for opcode in sorted(_INSTRUCTIONS_BY_OPCODE)
)
