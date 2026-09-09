// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

namespace Internal.TypeSystem.Interop
{
    public static class MarshalUtils
    {
        /// <summary>
        /// Returns true if this type has a common representation in both managed and unmanaged memory
        /// and does not require special handling by the interop marshaler.
        /// </summary>
        public static bool IsBlittableType(TypeDesc type)
        {
            if (!type.IsDefType)
            {
                return false;
            }

            DefType baseType = type.BaseType;
            bool hasNonTrivialParent = baseType != null
                && !baseType.IsWellKnownType(WellKnownType.Object)
                && !baseType.IsWellKnownType(WellKnownType.ValueType);

            // Type is blittable only if parent is also blittable.
            if (hasNonTrivialParent && !IsBlittableType(baseType))
            {
                return false;
            }

            var mdType = (MetadataType)type;

            if (!mdType.IsSequentialLayout && !mdType.IsExplicitLayout)
            {
                return false;
            }

            foreach (FieldDesc field in type.GetFields())
            {
                if (field.IsStatic)
                {
                    continue;
                }

                MarshallerKind marshallerKind = MarshalHelpers.GetMarshallerKind(
                    field.FieldType,
                    parameterIndex : null,
                    customModifierData: null,
                    field.GetMarshalAsDescriptor(),
                    isReturn: false,
                    isAnsi: mdType.PInvokeStringFormat == PInvokeStringFormat.AnsiClass,
                    MarshallerType.Field,
                    elementMarshallerKind: out var _);

                if (marshallerKind != MarshallerKind.Enum
                    && marshallerKind != MarshallerKind.BlittableValue
                    && marshallerKind != MarshallerKind.BlittableStruct
                    && marshallerKind != MarshallerKind.UnicodeChar)
                {
                    return false;
                }
            }

            return HasMatchingAppleArmLayout(mdType);
        }

        private static bool HasMatchingAppleArmLayout(MetadataType type)
        {
            TargetDetails target = type.Context.Target;
            if (target.Architecture != TargetArchitecture.ARM || !target.IsApplePlatform || type.IsPrimitive || type.IsEnum)
                return true;
            ClassLayoutMetadata layout = type.GetClassLayout();
            if (layout.PackingSize != 0)
                return true;

            // NativeStructType uses four-byte default packing on Darwin ARM.
            // A different native representation requires field marshalling,
            // even when all fields individually have blittable types.
            if (!type.IsValueType)
                return false;

            LayoutInt size = LayoutInt.Zero;
            LayoutInt alignment = LayoutInt.One;
            foreach (FieldDesc field in type.GetFields())
            {
                if (field.IsStatic)
                    continue;

                LayoutInt fieldSize = target.LayoutPointerSize;
                LayoutInt fieldAlignment = target.LayoutPointerSize;
                if (field.FieldType is DefType defType && defType.IsValueType)
                {
                    fieldSize = defType.InstanceFieldSize;
                    fieldAlignment = defType.InstanceFieldAlignment;
                }
                fieldAlignment = LayoutInt.Min(fieldAlignment, new LayoutInt(4));
                alignment = LayoutInt.Max(alignment, fieldAlignment);
                LayoutInt offset = type.IsExplicitLayout ? field.Offset : LayoutInt.AlignUp(size, fieldAlignment, target);
                if (offset != field.Offset)
                    return false;
                size = LayoutInt.Max(size, offset + fieldSize);
            }
            size = layout.Size == 0 ? LayoutInt.AlignUp(LayoutInt.Max(size, LayoutInt.One), alignment, target) :
                LayoutInt.Max(size, new LayoutInt(layout.Size));
            return size == type.InstanceFieldSize;
        }
    }
}
