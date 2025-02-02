// Copyright 2020 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "UsdBridgeUsdWriter.h"

#include "UsdBridgeUsdWriter_Common.h"

namespace
{
  template<typename ArrayType>
  ArrayType& GetStaticTempArray()
  {
    static ArrayType array;
    array.resize(0);
    return array;
  }

  template<typename UsdGeomType>
  UsdAttribute UsdGeomGetPointsAttribute(UsdGeomType& usdGeom) { return UsdAttribute(); }

  template<>
  UsdAttribute UsdGeomGetPointsAttribute(UsdGeomMesh& usdGeom) { return usdGeom.GetPointsAttr(); }

  template<>
  UsdAttribute UsdGeomGetPointsAttribute(UsdGeomPoints& usdGeom) { return usdGeom.GetPointsAttr(); }

  template<>
  UsdAttribute UsdGeomGetPointsAttribute(UsdGeomBasisCurves& usdGeom) { return usdGeom.GetPointsAttr(); }

  template<>
  UsdAttribute UsdGeomGetPointsAttribute(UsdGeomPointInstancer& usdGeom) { return usdGeom.GetPositionsAttr(); }

  // Array assignment
  template<class ArrayType>
  void AssignArrayToPrimvar(const void* data, size_t numElements, UsdAttribute& primvar, const UsdTimeCode& timeCode, ArrayType* usdArray)
  {
    using ElementType = typename ArrayType::ElementType;
    ElementType* typedData = (ElementType*)data;
    usdArray->assign(typedData, typedData + numElements);

    primvar.Set(*usdArray, timeCode);
  }

  template<class ArrayType>
  void AssignArrayToPrimvarFlatten(const void* data, UsdBridgeType dataType, size_t numElements, UsdAttribute& primvar, const UsdTimeCode& timeCode, ArrayType* usdArray)
  {
    int elementMultiplier = (int)dataType / UsdBridgeNumFundamentalTypes;
    size_t numFlattenedElements = numElements * elementMultiplier;

    AssignArrayToPrimvar<ArrayType>(data, numFlattenedElements, primvar, timeCode, usdArray);
  }

  template<class ArrayType, class EltType>
  void AssignArrayToPrimvarConvert(const void* data, size_t numElements, UsdAttribute& primvar, const UsdTimeCode& timeCode, ArrayType* usdArray)
  {
    using ElementType = typename ArrayType::ElementType;
    EltType* typedData = (EltType*)data;

    usdArray->resize(numElements);
    for (int i = 0; i < numElements; ++i)
    {
      (*usdArray)[i] = ElementType(typedData[i]);
    }

    primvar.Set(*usdArray, timeCode);
  }

  template<class ArrayType, class EltType>
  void AssignArrayToPrimvarConvertFlatten(const void* data, UsdBridgeType dataType, size_t numElements, UsdAttribute& primvar, const UsdTimeCode& timeCode, ArrayType* usdArray)
  {
    int elementMultiplier = (int)dataType / UsdBridgeNumFundamentalTypes;
    size_t numFlattenedElements = numElements * elementMultiplier;

    AssignArrayToPrimvarConvert<ArrayType, EltType>(data, numFlattenedElements, primvar, timeCode, usdArray);
  }

  template<typename ArrayType, typename EltType>
  void Expand1ToVec3(const void* data, uint64_t numElements, UsdAttribute& primvar, const UsdTimeCode& timeCode, ArrayType* usdArray)
  {
    usdArray->resize(numElements);
    const EltType* typedInput = reinterpret_cast<const EltType*>(data);
    for (int i = 0; i < numElements; ++i)
    {
      (*usdArray)[i] = typename ArrayType::ElementType(typedInput[i], typedInput[i], typedInput[i]);
    }
    primvar.Set(*usdArray, timeCode);
  }

  template<typename InputEltType, int numComponents>
  void ExpandToColor(const void* data, uint64_t numElements, UsdAttribute& primvar, const UsdTimeCode& timeCode, VtVec4fArray* usdArray)
  {
    usdArray->resize(numElements);
    const InputEltType* typedInput = reinterpret_cast<const InputEltType*>(data);
    // No memcopies, as input is not guaranteed to be of float type
    if(numComponents == 1)
      for (int i = 0; i < numElements; ++i)
        (*usdArray)[i] = GfVec4f(typedInput[i], 0.0f, 0.0f, 1.0f);
    if(numComponents == 2)
      for (int i = 0; i < numElements; ++i)
        (*usdArray)[i] = GfVec4f(typedInput[i*2], typedInput[i*2+1], 0.0f, 1.0f);
    if(numComponents == 3)
      for (int i = 0; i < numElements; ++i)
        (*usdArray)[i] = GfVec4f(typedInput[i*3], typedInput[i*3+1], typedInput[i*3+2], 1.0f);
    primvar.Set(*usdArray, timeCode);
  }

  template<typename InputEltType, int numComponents>
  void ExpandToColorNormalize(const void* data, uint64_t numElements, UsdAttribute& primvar, const UsdTimeCode& timeCode, VtVec4fArray* usdArray)
  {
    usdArray->resize(numElements);
    const InputEltType* typedInput = reinterpret_cast<const InputEltType*>(data);
    double normFactor = 1.0f / (double)std::numeric_limits<InputEltType>::max(); // float may not be enough for uint32_t
    // No memcopies, as input is not guaranteed to be of float type
    if(numComponents == 1)
      for (int i = 0; i < numElements; ++i)
        (*usdArray)[i] = GfVec4f(typedInput[i]*normFactor, 0.0f, 0.0f, 1.0f);
    if(numComponents == 2)
      for (int i = 0; i < numElements; ++i)
        (*usdArray)[i] = GfVec4f(typedInput[i*2]*normFactor, typedInput[i*2+1]*normFactor, 0.0f, 1.0f);
    if(numComponents == 3)
      for (int i = 0; i < numElements; ++i)
        (*usdArray)[i] = GfVec4f(typedInput[i*3]*normFactor, typedInput[i*3+1]*normFactor, typedInput[i*3+2]*normFactor, 1.0f);
    if(numComponents == 4)
      for (int i = 0; i < numElements; ++i)
        (*usdArray)[i] = GfVec4f(typedInput[i*4]*normFactor, typedInput[i*4+1]*normFactor, typedInput[i*4+2]*normFactor, typedInput[i*4+3]*normFactor);
    primvar.Set(*usdArray, timeCode);
  }

  #define ASSIGN_PRIMVAR_MACRO(ArrayType) \
    ArrayType& usdArray = GetStaticTempArray<ArrayType>(); AssignArrayToPrimvar<ArrayType>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray)
  #define ASSIGN_PRIMVAR_FLATTEN_MACRO(ArrayType) \
    ArrayType& usdArray = GetStaticTempArray<ArrayType>(); AssignArrayToPrimvarFlatten<ArrayType>(arrayData, arrayDataType, arrayNumElements, arrayPrimvar, timeCode, &usdArray)
  #define ASSIGN_PRIMVAR_CONVERT_MACRO(ArrayType, EltType) \
    ArrayType& usdArray = GetStaticTempArray<ArrayType>(); AssignArrayToPrimvarConvert<ArrayType, EltType>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray)
  #define ASSIGN_PRIMVAR_CONVERT_FLATTEN_MACRO(ArrayType, EltType) \
    ArrayType& usdArray = GetStaticTempArray<ArrayType>(); AssignArrayToPrimvarConvertFlatten<ArrayType, EltType>(arrayData, arrayDataType, arrayNumElements, arrayPrimvar, timeCode, &usdArray)
  #define ASSIGN_PRIMVAR_CUSTOM_ARRAY_MACRO(ArrayType, customArray) \
    AssignArrayToPrimvar<ArrayType>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &customArray)
  #define ASSIGN_PRIMVAR_CONVERT_CUSTOM_ARRAY_MACRO(ArrayType, EltType, customArray) \
    AssignArrayToPrimvarConvert<ArrayType, EltType>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &customArray)
  #define ASSIGN_PRIMVAR_MACRO_1EXPAND3(ArrayType, EltType) \
    ArrayType& usdArray = GetStaticTempArray<ArrayType>(); Expand1ToVec3<ArrayType, EltType>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);
  #define ASSIGN_PRIMVAR_MACRO_1EXPAND_COL(EltType) \
    VtVec4fArray& usdArray = GetStaticTempArray<VtVec4fArray>(); ExpandToColor<EltType, 1>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);
  #define ASSIGN_PRIMVAR_MACRO_2EXPAND_COL(EltType) \
    VtVec4fArray& usdArray = GetStaticTempArray<VtVec4fArray>(); ExpandToColor<EltType, 2>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);
  #define ASSIGN_PRIMVAR_MACRO_3EXPAND_COL(EltType) \
    VtVec4fArray& usdArray = GetStaticTempArray<VtVec4fArray>(); ExpandToColor<EltType, 3>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);
  #define ASSIGN_PRIMVAR_MACRO_1EXPAND_NORMALIZE_COL(EltType) \
    VtVec4fArray& usdArray = GetStaticTempArray<VtVec4fArray>(); ExpandToColorNormalize<EltType, 1>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);
  #define ASSIGN_PRIMVAR_MACRO_2EXPAND_NORMALIZE_COL(EltType) \
    VtVec4fArray& usdArray = GetStaticTempArray<VtVec4fArray>(); ExpandToColorNormalize<EltType, 2>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);
  #define ASSIGN_PRIMVAR_MACRO_3EXPAND_NORMALIZE_COL(EltType) \
    VtVec4fArray& usdArray = GetStaticTempArray<VtVec4fArray>(); ExpandToColorNormalize<EltType, 3>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);
  #define ASSIGN_PRIMVAR_MACRO_4EXPAND_NORMALIZE_COL(EltType) \
    VtVec4fArray& usdArray = GetStaticTempArray<VtVec4fArray>(); ExpandToColorNormalize<EltType, 4>(arrayData, arrayNumElements, arrayPrimvar, timeCode, &usdArray);

  void CopyArrayToPrimvar(UsdBridgeUsdWriter* writer, const void* arrayData, UsdBridgeType arrayDataType, size_t arrayNumElements, UsdAttribute arrayPrimvar, const UsdTimeCode& timeCode)
  {
    SdfValueTypeName primvarType = GetPrimvarArrayType(arrayDataType);

    switch (arrayDataType)
    {
      case UsdBridgeType::UCHAR: { ASSIGN_PRIMVAR_MACRO(VtUCharArray); break; }
      case UsdBridgeType::CHAR: { ASSIGN_PRIMVAR_MACRO(VtUCharArray); break; }
      case UsdBridgeType::USHORT: { ASSIGN_PRIMVAR_CONVERT_MACRO(VtUIntArray, short); break; }
      case UsdBridgeType::SHORT: { ASSIGN_PRIMVAR_CONVERT_MACRO(VtIntArray, unsigned short); break; }
      case UsdBridgeType::UINT: { ASSIGN_PRIMVAR_MACRO(VtUIntArray); break; }
      case UsdBridgeType::INT: { ASSIGN_PRIMVAR_MACRO(VtIntArray); break; }
      case UsdBridgeType::LONG: { ASSIGN_PRIMVAR_MACRO(VtInt64Array); break; }
      case UsdBridgeType::ULONG: { ASSIGN_PRIMVAR_MACRO(VtUInt64Array); break; }
      case UsdBridgeType::HALF: { ASSIGN_PRIMVAR_MACRO(VtHalfArray); break; }
      case UsdBridgeType::FLOAT: { ASSIGN_PRIMVAR_MACRO(VtFloatArray); break; }
      case UsdBridgeType::DOUBLE: { ASSIGN_PRIMVAR_MACRO(VtDoubleArray); break; }

      case UsdBridgeType::INT2: { ASSIGN_PRIMVAR_MACRO(VtVec2iArray); break; }
      case UsdBridgeType::FLOAT2: { ASSIGN_PRIMVAR_MACRO(VtVec2fArray); break; }
      case UsdBridgeType::DOUBLE2: { ASSIGN_PRIMVAR_MACRO(VtVec2dArray); break; }

      case UsdBridgeType::INT3: { ASSIGN_PRIMVAR_MACRO(VtVec3iArray); break; }
      case UsdBridgeType::FLOAT3: { ASSIGN_PRIMVAR_MACRO(VtVec3fArray); break; }
      case UsdBridgeType::DOUBLE3: { ASSIGN_PRIMVAR_MACRO(VtVec3dArray); break; }

      case UsdBridgeType::INT4: { ASSIGN_PRIMVAR_MACRO(VtVec4iArray); break; }
      case UsdBridgeType::FLOAT4: { ASSIGN_PRIMVAR_MACRO(VtVec4fArray); break; }
      case UsdBridgeType::DOUBLE4: { ASSIGN_PRIMVAR_MACRO(VtVec4dArray); break; }

      case UsdBridgeType::UCHAR2:
      case UsdBridgeType::UCHAR3: 
      case UsdBridgeType::UCHAR4: { ASSIGN_PRIMVAR_FLATTEN_MACRO(VtUCharArray); break; }
      case UsdBridgeType::CHAR2:
      case UsdBridgeType::CHAR3: 
      case UsdBridgeType::CHAR4: { ASSIGN_PRIMVAR_FLATTEN_MACRO(VtUCharArray); break; }
      case UsdBridgeType::USHORT2:
      case UsdBridgeType::USHORT3:
      case UsdBridgeType::USHORT4: { ASSIGN_PRIMVAR_CONVERT_FLATTEN_MACRO(VtUIntArray, short); break; }
      case UsdBridgeType::SHORT2:
      case UsdBridgeType::SHORT3: 
      case UsdBridgeType::SHORT4: { ASSIGN_PRIMVAR_CONVERT_FLATTEN_MACRO(VtIntArray, unsigned short); break; }
      case UsdBridgeType::UINT2:
      case UsdBridgeType::UINT3: 
      case UsdBridgeType::UINT4: { ASSIGN_PRIMVAR_FLATTEN_MACRO(VtUIntArray); break; }
      case UsdBridgeType::LONG2:
      case UsdBridgeType::LONG3: 
      case UsdBridgeType::LONG4: { ASSIGN_PRIMVAR_FLATTEN_MACRO(VtInt64Array); break; }
      case UsdBridgeType::ULONG2:
      case UsdBridgeType::ULONG3: 
      case UsdBridgeType::ULONG4: { ASSIGN_PRIMVAR_FLATTEN_MACRO(VtUInt64Array); break; }
      case UsdBridgeType::HALF2:
      case UsdBridgeType::HALF3: 
      case UsdBridgeType::HALF4: { ASSIGN_PRIMVAR_FLATTEN_MACRO(VtHalfArray); break; }

      default: {UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom Attribute<Index> primvar copy does not support source data type: " << arrayDataType) break; }
    };
  }

  template<typename GeomDataType>
  void CreateUsdGeomColorPrimvars(UsdGeomPrimvarsAPI& primvarApi, const GeomDataType& geomData, const UsdBridgeSettings& settings, const TimeEvaluator<GeomDataType>* timeEval = nullptr)
  {
    using DMI = typename GeomDataType::DataMemberId;

    bool timeVarChecked = true;
    if(timeEval)
    {
      timeVarChecked = timeEval->IsTimeVarying(DMI::COLORS);
    }

    if (timeVarChecked)
    {
      primvarApi.CreatePrimvar(UsdBridgeTokens->color, SdfValueTypeNames->Color4fArray);
    }
    else
    {
      primvarApi.RemovePrimvar(UsdBridgeTokens->color);
    }
  }

  template<typename GeomDataType>
  void CreateUsdGeomTexturePrimvars(UsdGeomPrimvarsAPI& primvarApi, const GeomDataType& geomData, const UsdBridgeSettings& settings, const TimeEvaluator<GeomDataType>* timeEval = nullptr)
  {
    using DMI = typename GeomDataType::DataMemberId;

    bool timeVarChecked = true;
    if(timeEval)
    {
      timeVarChecked = timeEval->IsTimeVarying(DMI::ATTRIBUTE0);
    }

    if (timeVarChecked)
      primvarApi.CreatePrimvar(UsdBridgeTokens->st, SdfValueTypeNames->TexCoord2fArray);
    else if (timeEval)
      primvarApi.RemovePrimvar(UsdBridgeTokens->st);
  }

  template<typename GeomDataType>
  void CreateUsdGeomAttributePrimvars(UsdGeomPrimvarsAPI& primvarApi, const GeomDataType& geomData, const TimeEvaluator<GeomDataType>* timeEval = nullptr)
  {
    using DMI = typename GeomDataType::DataMemberId;

    for(uint32_t attribIndex = 0; attribIndex < geomData.NumAttributes; ++attribIndex)
    {
      const UsdBridgeAttribute& attrib = geomData.Attributes[attribIndex];
      if(attrib.DataType != UsdBridgeType::UNDEFINED)
      {
        bool timeVarChecked = true;
        if(timeEval)
        {
          DMI attributeId = DMI::ATTRIBUTE0 + attribIndex;
          timeVarChecked = timeEval->IsTimeVarying(attributeId);
        }

        if(timeVarChecked)
        {
          SdfValueTypeName primvarType = GetPrimvarArrayType(attrib.DataType);
          primvarApi.CreatePrimvar(AttribIndexToToken(attribIndex), primvarType);
        }
        else if(timeEval)
        {
          primvarApi.RemovePrimvar(AttribIndexToToken(attribIndex));
        }
      }
    }
  }

  void InitializeUsdGeometryTimeVar(UsdGeomMesh& meshGeom, const UsdBridgeMeshData& meshData, const UsdBridgeSettings& settings, 
    const TimeEvaluator<UsdBridgeMeshData>* timeEval = nullptr)
  {
    typedef UsdBridgeMeshData::DataMemberId DMI;
    UsdGeomPrimvarsAPI primvarApi(meshGeom);

    UsdPrim meshPrim = meshGeom.GetPrim();

    if (!timeEval || timeEval->IsTimeVarying(DMI::POINTS))
    {
      meshGeom.CreatePointsAttr();
      meshGeom.CreateExtentAttr();
    }
    else
    {
      meshPrim.RemoveProperty(UsdBridgeTokens->points);
      meshPrim.RemoveProperty(UsdBridgeTokens->extent);
    }

    if (!timeEval || timeEval->IsTimeVarying(DMI::INDICES))
    {
      meshGeom.CreateFaceVertexIndicesAttr();
      meshGeom.CreateFaceVertexCountsAttr();
    }
    else
    {
      meshPrim.RemoveProperty(UsdBridgeTokens->faceVertexCounts);
      meshPrim.RemoveProperty(UsdBridgeTokens->faceVertexIndices);
    }

    if (!timeEval || timeEval->IsTimeVarying(DMI::NORMALS))
      meshGeom.CreateNormalsAttr();
    else
      meshPrim.RemoveProperty(UsdBridgeTokens->normals);

    CreateUsdGeomColorPrimvars(primvarApi, meshData, settings, timeEval);

    if(settings.EnableStTexCoords)
      CreateUsdGeomTexturePrimvars(primvarApi, meshData, settings, timeEval);

    CreateUsdGeomAttributePrimvars(primvarApi, meshData, timeEval);
  }
  
  void InitializeUsdGeometryTimeVar(UsdGeomPoints& pointsGeom, const UsdBridgeInstancerData& instancerData, const UsdBridgeSettings& settings,
    const TimeEvaluator<UsdBridgeInstancerData>* timeEval = nullptr)
  {
    typedef UsdBridgeInstancerData::DataMemberId DMI;
    UsdGeomPrimvarsAPI primvarApi(pointsGeom);

    UsdPrim pointsPrim = pointsGeom.GetPrim();

    if (!timeEval || timeEval->IsTimeVarying(DMI::POINTS))
    {
      pointsGeom.CreatePointsAttr();
      pointsGeom.CreateExtentAttr();
    }
    else
    {
      pointsPrim.RemoveProperty(UsdBridgeTokens->points);
      pointsPrim.RemoveProperty(UsdBridgeTokens->extent);
    }

    if (!timeEval || timeEval->IsTimeVarying(DMI::INSTANCEIDS))
      pointsGeom.CreateIdsAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->ids);

    if (!timeEval || timeEval->IsTimeVarying(DMI::ORIENTATIONS))
      pointsGeom.CreateNormalsAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->normals);

    if (!timeEval || timeEval->IsTimeVarying(DMI::SCALES))
      pointsGeom.CreateWidthsAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->widths);

    CreateUsdGeomColorPrimvars(primvarApi, instancerData, settings, timeEval);

    if(settings.EnableStTexCoords)
      CreateUsdGeomTexturePrimvars(primvarApi, instancerData, settings, timeEval);

    CreateUsdGeomAttributePrimvars(primvarApi, instancerData, timeEval);
  }

  void InitializeUsdGeometryTimeVar(UsdGeomPointInstancer& pointsGeom, const UsdBridgeInstancerData& instancerData, const UsdBridgeSettings& settings,
    const TimeEvaluator<UsdBridgeInstancerData>* timeEval = nullptr)
  {
    typedef UsdBridgeInstancerData::DataMemberId DMI;
    UsdGeomPrimvarsAPI primvarApi(pointsGeom);

    UsdPrim pointsPrim = pointsGeom.GetPrim();

    if (!timeEval || timeEval->IsTimeVarying(DMI::POINTS))
    {
      pointsGeom.CreatePositionsAttr();
      pointsGeom.CreateExtentAttr();
    }
    else
    {
      pointsPrim.RemoveProperty(UsdBridgeTokens->positions);
      pointsPrim.RemoveProperty(UsdBridgeTokens->extent);
    }

    if (!timeEval || timeEval->IsTimeVarying(DMI::SHAPEINDICES))
      pointsGeom.CreateProtoIndicesAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->protoIndices);

    if (!timeEval || timeEval->IsTimeVarying(DMI::INSTANCEIDS))
      pointsGeom.CreateIdsAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->ids);

    if (!timeEval || timeEval->IsTimeVarying(DMI::ORIENTATIONS))
      pointsGeom.CreateOrientationsAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->orientations);

    if (!timeEval || timeEval->IsTimeVarying(DMI::SCALES))
      pointsGeom.CreateScalesAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->scales);

    CreateUsdGeomColorPrimvars(primvarApi, instancerData, settings, timeEval);

    if(settings.EnableStTexCoords)
      CreateUsdGeomTexturePrimvars(primvarApi, instancerData, settings, timeEval);

    CreateUsdGeomAttributePrimvars(primvarApi, instancerData, timeEval);

    if (!timeEval || timeEval->IsTimeVarying(DMI::LINEARVELOCITIES))
      pointsGeom.CreateVelocitiesAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->velocities);

    if (!timeEval || timeEval->IsTimeVarying(DMI::ANGULARVELOCITIES))
      pointsGeom.CreateAngularVelocitiesAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->angularVelocities);

    if (!timeEval || timeEval->IsTimeVarying(DMI::INVISIBLEIDS))
      pointsGeom.CreateInvisibleIdsAttr();
    else
      pointsPrim.RemoveProperty(UsdBridgeTokens->invisibleIds);
  }

  void InitializeUsdGeometryTimeVar(UsdGeomBasisCurves& curveGeom, const UsdBridgeCurveData& curveData, const UsdBridgeSettings& settings,
    const TimeEvaluator<UsdBridgeCurveData>* timeEval = nullptr)
  {
    typedef UsdBridgeCurveData::DataMemberId DMI;
    UsdGeomPrimvarsAPI primvarApi(curveGeom);

    UsdPrim curvePrim = curveGeom.GetPrim();

    if (!timeEval || timeEval->IsTimeVarying(DMI::POINTS))
    {
      curveGeom.CreatePointsAttr();
      curveGeom.CreateExtentAttr();
    }
    else
    {
      curvePrim.RemoveProperty(UsdBridgeTokens->positions);
      curvePrim.RemoveProperty(UsdBridgeTokens->extent);
    }

    if (!timeEval || timeEval->IsTimeVarying(DMI::CURVELENGTHS))
      curveGeom.CreateCurveVertexCountsAttr();
    else
      curvePrim.RemoveProperty(UsdBridgeTokens->curveVertexCounts);

    if (!timeEval || timeEval->IsTimeVarying(DMI::NORMALS))
      curveGeom.CreateNormalsAttr();
    else
      curvePrim.RemoveProperty(UsdBridgeTokens->normals);

    if (!timeEval || timeEval->IsTimeVarying(DMI::SCALES))
      curveGeom.CreateWidthsAttr();
    else
      curvePrim.RemoveProperty(UsdBridgeTokens->widths);

    CreateUsdGeomColorPrimvars(primvarApi, curveData, settings, timeEval);

    if(settings.EnableStTexCoords)
      CreateUsdGeomTexturePrimvars(primvarApi, curveData, settings, timeEval);

    CreateUsdGeomAttributePrimvars(primvarApi, curveData, timeEval);

  }

  UsdPrim InitializeUsdGeometry_Impl(UsdStageRefPtr geometryStage, const SdfPath& geomPath, const UsdBridgeMeshData& meshData, bool uniformPrim,
    const UsdBridgeSettings& settings,
    TimeEvaluator<UsdBridgeMeshData>* timeEval = nullptr)
  {
    UsdGeomMesh geomMesh = GetOrDefinePrim<UsdGeomMesh>(geometryStage, geomPath);
    
    InitializeUsdGeometryTimeVar(geomMesh, meshData, settings, timeEval);

    if (uniformPrim)
    {
      geomMesh.CreateDoubleSidedAttr(VtValue(true));
      geomMesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    }

    return geomMesh.GetPrim();
  }

  UsdPrim InitializeUsdGeometry_Impl(UsdStageRefPtr geometryStage, const SdfPath& geomPath, const UsdBridgeInstancerData& instancerData, bool uniformPrim,
    const UsdBridgeSettings& settings,
    TimeEvaluator<UsdBridgeInstancerData>* timeEval = nullptr)
  {
    if (UsesUsdGeomPoints(instancerData))
    {
      UsdGeomPoints geomPoints = GetOrDefinePrim<UsdGeomPoints>(geometryStage, geomPath);
      
      InitializeUsdGeometryTimeVar(geomPoints, instancerData, settings, timeEval);

      if (uniformPrim)
      {
        geomPoints.CreateDoubleSidedAttr(VtValue(true));
      }

      return geomPoints.GetPrim();
    }
    else
    {
      UsdGeomPointInstancer geomPoints = GetOrDefinePrim<UsdGeomPointInstancer>(geometryStage, geomPath);
      
      InitializeUsdGeometryTimeVar(geomPoints, instancerData, settings, timeEval);

      if (uniformPrim)
      {
        // Initialize the point instancer with a sphere shape
        SdfPath shapePath;
        switch (instancerData.Shapes[0])
        {
          case UsdBridgeInstancerData::SHAPE_SPHERE:
          {
            shapePath = geomPath.AppendPath(SdfPath("sphere"));
            UsdGeomSphere::Define(geometryStage, shapePath);
            break;
          }
          case UsdBridgeInstancerData::SHAPE_CYLINDER:
          {
            shapePath = geomPath.AppendPath(SdfPath("cylinder"));
            UsdGeomCylinder::Define(geometryStage, shapePath);
            break;
          }
          case UsdBridgeInstancerData::SHAPE_CONE:
          {
            shapePath = geomPath.AppendPath(SdfPath("cone"));
            UsdGeomCone::Define(geometryStage, shapePath);
            break;
          }
        }

        UsdRelationship protoRel = geomPoints.GetPrototypesRel();
        protoRel.AddTarget(shapePath);
      }

      return geomPoints.GetPrim();
    }
  }

  UsdPrim InitializeUsdGeometry_Impl(UsdStageRefPtr geometryStage, const SdfPath& geomPath, const UsdBridgeCurveData& curveData, bool uniformPrim,
    const UsdBridgeSettings& settings,
    TimeEvaluator<UsdBridgeCurveData>* timeEval = nullptr)
  {
    UsdGeomBasisCurves geomCurves = GetOrDefinePrim<UsdGeomBasisCurves>(geometryStage, geomPath);

    InitializeUsdGeometryTimeVar(geomCurves, curveData, settings, timeEval);

    if (uniformPrim)
    {
      geomCurves.CreateDoubleSidedAttr(VtValue(true));
      geomCurves.GetTypeAttr().Set(UsdGeomTokens->linear);
    }

    return geomCurves.GetPrim();
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomPoints(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::POINTS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::POINTS);

    ClearUsdAttributes(UsdGeomGetPointsAttribute(uniformGeom), UsdGeomGetPointsAttribute(timeVarGeom), timeVaryingUpdate);
    ClearUsdAttributes(uniformGeom.GetExtentAttr(), timeVarGeom.GetExtentAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      if (!geomData.Points)
      {
        UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "GeomData requires points.");
      }
      else
      {
        UsdGeomType* outGeom = timeVaryingUpdate ? &timeVarGeom : &uniformGeom;
        UsdTimeCode timeCode = timeEval.Eval(DMI::POINTS);

        // Points
        UsdAttribute pointsAttr = UsdGeomGetPointsAttribute(*outGeom);

        const void* arrayData = geomData.Points;
        size_t arrayNumElements = geomData.NumPoints;
        UsdAttribute arrayPrimvar = pointsAttr;
        VtVec3fArray& usdVerts = GetStaticTempArray<VtVec3fArray>();
        switch (geomData.PointsType)
        {
        case UsdBridgeType::FLOAT3: {ASSIGN_PRIMVAR_CUSTOM_ARRAY_MACRO(VtVec3fArray, usdVerts); break; }
        case UsdBridgeType::DOUBLE3: {ASSIGN_PRIMVAR_CONVERT_CUSTOM_ARRAY_MACRO(VtVec3fArray, GfVec3d, usdVerts); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom PointsAttr should be FLOAT3 or DOUBLE3."); break; }
        }

        // Usd requires extent.
        GfRange3f extent;
        for (const auto& pt : usdVerts) {
          extent.UnionWith(pt);
        }
        VtVec3fArray extentArray(2);
        extentArray[0] = extent.GetMin();
        extentArray[1] = extent.GetMax();

        outGeom->GetExtentAttr().Set(extentArray, timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomIndices(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::INDICES);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::INDICES);

    ClearUsdAttributes(uniformGeom.GetFaceVertexIndicesAttr(), timeVarGeom.GetFaceVertexIndicesAttr(), timeVaryingUpdate);
    ClearUsdAttributes(uniformGeom.GetFaceVertexCountsAttr(), timeVarGeom.GetFaceVertexCountsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType* outGeom = timeVaryingUpdate ? &timeVarGeom : &uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::INDICES);

      uint64_t numIndices = geomData.NumIndices;
    
      VtArray<int>& usdVertexCounts = GetStaticTempArray<VtArray<int>>();
      usdVertexCounts.resize(numPrims);
      int vertexCount = numIndices / numPrims;
      for (uint64_t i = 0; i < numPrims; ++i)
        usdVertexCounts[i] = vertexCount;//geomData.FaceVertCounts[i];

      // Face Vertex counts
      UsdAttribute faceVertCountsAttr = outGeom->GetFaceVertexCountsAttr();
      faceVertCountsAttr.Set(usdVertexCounts, timeCode);

      if (!geomData.Indices)
      {
        writer->TempIndexArray.resize(numIndices);
        for (uint64_t i = 0; i < numIndices; ++i)
          writer->TempIndexArray[i] = (int)i;

        UsdAttribute arrayPrimvar = outGeom->GetFaceVertexIndicesAttr();
        arrayPrimvar.Set(writer->TempIndexArray, timeCode);
      }
      else
      {
        // Face indices
        const void* arrayData = geomData.Indices;
        size_t arrayNumElements = numIndices;
        UsdAttribute arrayPrimvar = outGeom->GetFaceVertexIndicesAttr();
        switch (geomData.IndicesType)
        {
        case UsdBridgeType::ULONG: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtIntArray, uint64_t); break; }
        case UsdBridgeType::LONG: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtIntArray, int64_t); break; }
        case UsdBridgeType::INT: {ASSIGN_PRIMVAR_MACRO(VtIntArray); break; }
        case UsdBridgeType::UINT: {ASSIGN_PRIMVAR_MACRO(VtIntArray); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom FaceVertexIndicesAttr should be (U)LONG or (U)INT."); break; }
        }
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomNormals(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::NORMALS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::NORMALS);

    ClearUsdAttributes(uniformGeom.GetNormalsAttr(), timeVarGeom.GetNormalsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType* outGeom = timeVaryingUpdate ? &timeVarGeom : &uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::NORMALS);

      UsdAttribute normalsAttr = outGeom->GetNormalsAttr();

      if (geomData.Normals != nullptr)
      {
        const void* arrayData = geomData.Normals;
        size_t arrayNumElements = geomData.PerPrimNormals ? numPrims : geomData.NumPoints;
        UsdAttribute arrayPrimvar = normalsAttr;
        switch (geomData.NormalsType)
        {
        case UsdBridgeType::FLOAT3: {ASSIGN_PRIMVAR_MACRO(VtVec3fArray); break; }
        case UsdBridgeType::DOUBLE3: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtVec3fArray, GfVec3d); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom NormalsAttr should be FLOAT3 or DOUBLE3."); break; }
        }

        // Per face or per-vertex interpolation. This will break timesteps that have been written before.
        TfToken normalInterpolation = geomData.PerPrimNormals ? UsdGeomTokens->uniform : UsdGeomTokens->vertex;
        uniformGeom.SetNormalsInterpolation(normalInterpolation);
      }
      else
      {
        normalsAttr.Set(SdfValueBlock(), timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomTexCoords(UsdBridgeUsdWriter* writer, UsdGeomPrimvarsAPI& timeVarPrimvars, UsdGeomType& uniformPrimvars, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::ATTRIBUTE0);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::ATTRIBUTE0);

    UsdGeomPrimvar uniformPrimvar = uniformPrimvars.GetPrimvar(UsdBridgeTokens->st);
    UsdGeomPrimvar timeVarPrimvar = timeVarPrimvars.GetPrimvar(UsdBridgeTokens->st);

    ClearUsdAttributes(uniformPrimvar.GetAttr(), timeVarPrimvar.GetAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdTimeCode timeCode = timeEval.Eval(DMI::ATTRIBUTE0);

      UsdAttribute texcoordPrimvar = timeVaryingUpdate ? timeVarPrimvar : uniformPrimvar;
      assert(texcoordPrimvar);

      const UsdBridgeAttribute& texCoordAttrib = geomData.Attributes[0];

      if (texCoordAttrib.Data != nullptr)
      {
        const void* arrayData = texCoordAttrib.Data;
        size_t arrayNumElements = texCoordAttrib.PerPrimData ? numPrims : geomData.NumPoints;
        UsdAttribute arrayPrimvar = texcoordPrimvar;

        switch (texCoordAttrib.DataType)
        {
        case UsdBridgeType::FLOAT2: { ASSIGN_PRIMVAR_MACRO(VtVec2fArray); break; }
        case UsdBridgeType::DOUBLE2: { ASSIGN_PRIMVAR_CONVERT_MACRO(VtVec2fArray, GfVec2d); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom st primvar should be FLOAT2 or DOUBLE2."); break; }
        }
  
        // Per face or per-vertex interpolation. This will break timesteps that have been written before.
        TfToken texcoordInterpolation = texCoordAttrib.PerPrimData ? UsdGeomTokens->uniform : UsdGeomTokens->vertex;
        uniformPrimvar.SetInterpolation(texcoordInterpolation);
      }
      else
      {
        texcoordPrimvar.Set(SdfValueBlock(), timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomAttribute(UsdBridgeUsdWriter* writer, UsdGeomPrimvarsAPI& timeVarPrimvars, UsdGeomType& uniformPrimvars, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval, uint32_t attribIndex)
  {
    assert(attribIndex < geomData.NumAttributes);
    const UsdBridgeAttribute& bridgeAttrib = geomData.Attributes[attribIndex];

    TfToken attribToken = AttribIndexToToken(attribIndex);
    UsdGeomPrimvar uniformPrimvar = uniformPrimvars.GetPrimvar(attribToken);
    UsdGeomPrimvar timeVarPrimvar = timeVarPrimvars.GetPrimvar(attribToken);

    using DMI = typename GeomDataType::DataMemberId;
    DMI attributeId = DMI::ATTRIBUTE0 + attribIndex;
    bool performsUpdate = updateEval.PerformsUpdate(attributeId);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(attributeId);

    ClearUsdAttributes(uniformPrimvar.GetAttr(), timeVarPrimvar.GetAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdTimeCode timeCode = timeEval.Eval(attributeId);

      UsdAttribute attributePrimvar = timeVaryingUpdate ? timeVarPrimvar : uniformPrimvar;

      if(!attributePrimvar)
      {
        UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom Attribute<Index> primvar not found, was the attribute at requested index valid during initialization of the prim? Index is " << attribIndex);
      }
      else
      {
        if (bridgeAttrib.Data != nullptr)
        {
          const void* arrayData = bridgeAttrib.Data;
          size_t arrayNumElements = bridgeAttrib.PerPrimData ? numPrims : geomData.NumPoints;
          UsdAttribute arrayPrimvar = attributePrimvar;

          CopyArrayToPrimvar(writer, arrayData, bridgeAttrib.DataType, arrayNumElements, arrayPrimvar, timeCode);
    
          // Per face or per-vertex interpolation. This will break timesteps that have been written before.
          TfToken attribInterpolation = bridgeAttrib.PerPrimData ? UsdGeomTokens->uniform : UsdGeomTokens->vertex;
          uniformPrimvar.SetInterpolation(attribInterpolation);
        }
        else
        {
          attributePrimvar.Set(SdfValueBlock(), timeCode);
        }
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomAttributes(UsdBridgeUsdWriter* writer, UsdGeomPrimvarsAPI& timeVarPrimvars, UsdGeomType& uniformPrimvars, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    uint32_t startIdx = 0;
    for(uint32_t attribIndex = startIdx; attribIndex < geomData.NumAttributes; ++attribIndex)
    {
      const UsdBridgeAttribute& attrib = geomData.Attributes[attribIndex];
      if(attrib.DataType != UsdBridgeType::UNDEFINED)
        UpdateUsdGeomAttribute(writer, timeVarPrimvars, uniformPrimvars, geomData, numPrims, updateEval, timeEval, attribIndex);
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomColors(UsdBridgeUsdWriter* writer, UsdGeomPrimvarsAPI& timeVarPrimvars, UsdGeomType& uniformPrimvars, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::COLORS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::COLORS);

    UsdGeomPrimvar uniformDispPrimvar = uniformPrimvars.GetPrimvar(UsdBridgeTokens->color);
    UsdGeomPrimvar timeVarDispPrimvar = timeVarPrimvars.GetPrimvar(UsdBridgeTokens->color);

    ClearUsdAttributes(uniformDispPrimvar.GetAttr(), timeVarDispPrimvar.GetAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdTimeCode timeCode = timeEval.Eval(DMI::COLORS);

      UsdGeomPrimvar colorPrimvar = timeVaryingUpdate ? timeVarDispPrimvar : uniformDispPrimvar;

      if (geomData.Colors != nullptr)
      {
        const void* arrayData = geomData.Colors;
        size_t arrayNumElements = geomData.PerPrimColors ? numPrims : geomData.NumPoints;
        TfToken colorInterpolation = geomData.PerPrimColors ? UsdGeomTokens->uniform : UsdGeomTokens->vertex;

        assert(colorPrimvar);

        UsdAttribute arrayPrimvar = colorPrimvar;
        switch (geomData.ColorsType)
        {
        case UsdBridgeType::UCHAR: {ASSIGN_PRIMVAR_MACRO_1EXPAND_NORMALIZE_COL(uint8_t); break; }
        case UsdBridgeType::UCHAR2: {ASSIGN_PRIMVAR_MACRO_2EXPAND_NORMALIZE_COL(uint8_t); break; }
        case UsdBridgeType::UCHAR3: {ASSIGN_PRIMVAR_MACRO_3EXPAND_NORMALIZE_COL(uint8_t); break; }
        case UsdBridgeType::UCHAR4: {ASSIGN_PRIMVAR_MACRO_4EXPAND_NORMALIZE_COL(uint8_t); break; }
        case UsdBridgeType::USHORT: {ASSIGN_PRIMVAR_MACRO_1EXPAND_NORMALIZE_COL(uint16_t); break; }
        case UsdBridgeType::USHORT2: {ASSIGN_PRIMVAR_MACRO_2EXPAND_NORMALIZE_COL(uint16_t); break; }
        case UsdBridgeType::USHORT3: {ASSIGN_PRIMVAR_MACRO_3EXPAND_NORMALIZE_COL(uint16_t); break; }
        case UsdBridgeType::USHORT4: {ASSIGN_PRIMVAR_MACRO_4EXPAND_NORMALIZE_COL(uint16_t); break; }
        case UsdBridgeType::UINT: {ASSIGN_PRIMVAR_MACRO_1EXPAND_NORMALIZE_COL(uint32_t); break; }
        case UsdBridgeType::UINT2: {ASSIGN_PRIMVAR_MACRO_2EXPAND_NORMALIZE_COL(uint32_t); break; }
        case UsdBridgeType::UINT3: {ASSIGN_PRIMVAR_MACRO_3EXPAND_NORMALIZE_COL(uint32_t); break; }
        case UsdBridgeType::UINT4: {ASSIGN_PRIMVAR_MACRO_4EXPAND_NORMALIZE_COL(uint32_t); break; }
        case UsdBridgeType::FLOAT: {ASSIGN_PRIMVAR_MACRO_1EXPAND_COL(float); break; }
        case UsdBridgeType::FLOAT2: {ASSIGN_PRIMVAR_MACRO_2EXPAND_COL(float); break; }
        case UsdBridgeType::FLOAT3: {ASSIGN_PRIMVAR_MACRO_3EXPAND_COL(float); break; }
        case UsdBridgeType::FLOAT4: {ASSIGN_PRIMVAR_MACRO(VtVec4fArray); break; }
        case UsdBridgeType::DOUBLE: {ASSIGN_PRIMVAR_MACRO_1EXPAND_COL(double) break; }
        case UsdBridgeType::DOUBLE2: {ASSIGN_PRIMVAR_MACRO_2EXPAND_COL(double); break; }
        case UsdBridgeType::DOUBLE3: {ASSIGN_PRIMVAR_MACRO_3EXPAND_COL(double); break; }
        case UsdBridgeType::DOUBLE4: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtVec4fArray, GfVec4d); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom color primvar is not of type (UCHAR/USHORT/UINT/FLOAT/DOUBLE)(1/2/3/4)."); break; }
        }

        // Per face or per-vertex interpolation. This will break timesteps that have been written before.
        uniformDispPrimvar.SetInterpolation(colorInterpolation);
      }
      else
      {
        colorPrimvar.GetAttr().Set(SdfValueBlock(), timeCode);
      }
    }
  }


  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomInstanceIds(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::INSTANCEIDS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::INSTANCEIDS);

    ClearUsdAttributes(uniformGeom.GetIdsAttr(), timeVarGeom.GetIdsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType* outGeom = timeVaryingUpdate ? &timeVarGeom : &uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::INSTANCEIDS);

      UsdAttribute idsAttr = outGeom->GetIdsAttr();

      if (geomData.InstanceIds)
      {
        const void* arrayData = geomData.InstanceIds;
        size_t arrayNumElements = geomData.NumPoints;
        UsdAttribute arrayPrimvar = idsAttr;
        switch (geomData.InstanceIdsType)
        {
        case UsdBridgeType::UINT: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtInt64Array, unsigned int); break; }
        case UsdBridgeType::INT: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtInt64Array, int); break; }
        case UsdBridgeType::LONG: {ASSIGN_PRIMVAR_MACRO(VtInt64Array); break; }
        case UsdBridgeType::ULONG: {ASSIGN_PRIMVAR_MACRO(VtInt64Array); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom IdsAttribute should be (U)LONG or (U)INT."); break; }
        }
      }
      else
      {
        idsAttr.Set(SdfValueBlock(), timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomWidths(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::SCALES);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::SCALES);

    ClearUsdAttributes(uniformGeom.GetWidthsAttr(), timeVarGeom.GetWidthsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::SCALES);

      UsdAttribute widthsAttribute = outGeom.GetWidthsAttr();
      assert(widthsAttribute);
      if (geomData.Scales)
      {
        const void* arrayData = geomData.Scales;
        size_t arrayNumElements = geomData.NumPoints;
        UsdAttribute arrayPrimvar = widthsAttribute;
        switch (geomData.ScalesType)
        {
        case UsdBridgeType::FLOAT: {ASSIGN_PRIMVAR_MACRO(VtFloatArray); break; }
        case UsdBridgeType::DOUBLE: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtFloatArray, double); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom WidthsAttribute should be FLOAT or DOUBLE."); break; }
        }
      }
      else
      {
        VtFloatArray& usdWidths = GetStaticTempArray<VtFloatArray>();
        usdWidths.resize(geomData.NumPoints);
        for(auto& x : usdWidths) x = (float)geomData.UniformScale;
        widthsAttribute.Set(usdWidths, timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomScales(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::SCALES);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::SCALES);

    ClearUsdAttributes(uniformGeom.GetScalesAttr(), timeVarGeom.GetScalesAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::SCALES);

      UsdAttribute scalesAttribute = outGeom.GetScalesAttr();
      assert(scalesAttribute);
      if (geomData.Scales)
      {
        const void* arrayData = geomData.Scales;
        size_t arrayNumElements = geomData.NumPoints;
        UsdAttribute arrayPrimvar = scalesAttribute;
        switch (geomData.ScalesType)
        {
        case UsdBridgeType::FLOAT: {ASSIGN_PRIMVAR_MACRO_1EXPAND3(VtVec3fArray, float); break;}
        case UsdBridgeType::DOUBLE: {ASSIGN_PRIMVAR_MACRO_1EXPAND3(VtVec3fArray, double); break;}
        case UsdBridgeType::FLOAT3: {ASSIGN_PRIMVAR_MACRO(VtVec3fArray); break; }
        case UsdBridgeType::DOUBLE3: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtVec3fArray, GfVec3d); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom ScalesAttribute should be FLOAT(3) or DOUBLE(3)."); break; }
        }
      }
      else
      {
        double pointScale = geomData.UniformScale;
        GfVec3f defaultScale((float)pointScale, (float)pointScale, (float)pointScale);
        VtVec3fArray& usdScales = GetStaticTempArray<VtVec3fArray>();
        usdScales.resize(geomData.NumPoints);
        for(auto& x : usdScales) x = defaultScale;
        scalesAttribute.Set(usdScales, timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomOrientNormals(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::ORIENTATIONS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::ORIENTATIONS);

    ClearUsdAttributes(uniformGeom.GetNormalsAttr(), timeVarGeom.GetNormalsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::ORIENTATIONS);

      UsdAttribute normalsAttribute = outGeom.GetNormalsAttr();
      assert(normalsAttribute);
      if (geomData.Orientations)
      {
        const void* arrayData = geomData.Orientations;
        size_t arrayNumElements = geomData.NumPoints;
        UsdAttribute arrayPrimvar = normalsAttribute;
        switch (geomData.OrientationsType)
        {
        case UsdBridgeType::FLOAT3: {ASSIGN_PRIMVAR_MACRO(VtVec3fArray); break; }
        case UsdBridgeType::DOUBLE3: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtVec3fArray, GfVec3d); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom NormalsAttribute (orientations) should be FLOAT3 or DOUBLE3."); break; }
        }
      }
      else
      {
        //Always provide a default orientation
        GfVec3f defaultNormal(1, 0, 0);
        VtVec3fArray& usdNormals = GetStaticTempArray<VtVec3fArray>();
        usdNormals.resize(geomData.NumPoints);
        for(auto& x : usdNormals) x = defaultNormal;
        normalsAttribute.Set(usdNormals, timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomOrientations(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::ORIENTATIONS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::ORIENTATIONS);

    ClearUsdAttributes(uniformGeom.GetOrientationsAttr(), timeVarGeom.GetOrientationsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::ORIENTATIONS);

      // Orientations
      UsdAttribute orientationsAttribute = outGeom.GetOrientationsAttr();
      assert(orientationsAttribute);
      VtQuathArray& usdOrients = GetStaticTempArray<VtQuathArray>();
      if (geomData.Orientations)
      {
        usdOrients.resize(geomData.NumPoints);
        switch (geomData.OrientationsType)
        {
        case UsdBridgeType::FLOAT3: { ConvertNormalsToQuaternions<float>(usdOrients, geomData.Orientations, geomData.NumPoints); break; }
        case UsdBridgeType::DOUBLE3: { ConvertNormalsToQuaternions<double>(usdOrients, geomData.Orientations, geomData.NumPoints); break; }
        case UsdBridgeType::FLOAT4: 
          { 
            for (uint64_t i = 0; i < geomData.NumPoints; ++i)
            {
              const float* orients = reinterpret_cast<const float*>(geomData.Orientations);
              usdOrients[i] = GfQuath(orients[i * 4], orients[i * 4 + 1], orients[i * 4 + 2], orients[i * 4 + 3]);
            }
            orientationsAttribute.Set(usdOrients, timeCode);
            break; 
          }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom OrientationsAttribute should be FLOAT3, DOUBLE3 or FLOAT4."); break; }
        }
        orientationsAttribute.Set(usdOrients, timeCode);
      }
      else
      {
        //Always provide a default orientation
        GfQuath defaultOrient(1, 0, 0, 0);
        usdOrients.resize(geomData.NumPoints);
        for(auto& x : usdOrients) x = defaultOrient;
        orientationsAttribute.Set(usdOrients, timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomShapeIndices(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;

    UsdTimeCode timeCode = timeEval.Eval(DMI::SHAPEINDICES);
    UsdGeomType* outGeom = timeCode.IsDefault() ? &uniformGeom : &timeVarGeom;
    
    //Shape indices
    UsdAttribute protoIndexAttr = outGeom->GetProtoIndicesAttr();
    VtIntArray& protoIndices = GetStaticTempArray<VtIntArray>();
    protoIndices.resize(geomData.NumPoints);
    for(auto& x : protoIndices) x = 0;
    protoIndexAttr.Set(protoIndices, timeCode);
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomLinearVelocities(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::LINEARVELOCITIES);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::LINEARVELOCITIES);

    ClearUsdAttributes(uniformGeom.GetVelocitiesAttr(), timeVarGeom.GetVelocitiesAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::LINEARVELOCITIES);

      // Linear velocities
      UsdAttribute linearVelocitiesAttribute = outGeom.GetVelocitiesAttr();
      assert(linearVelocitiesAttribute);
      if (geomData.LinearVelocities)
      {
        GfVec3f* linVels = (GfVec3f*)geomData.LinearVelocities;

        VtVec3fArray& usdVelocities = GetStaticTempArray<VtVec3fArray>();
        usdVelocities.assign(linVels, linVels + geomData.NumPoints);
        linearVelocitiesAttribute.Set(usdVelocities, timeCode);
      }
      else
      {
        linearVelocitiesAttribute.Set(SdfValueBlock(), timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomAngularVelocities(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::ANGULARVELOCITIES);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::ANGULARVELOCITIES);

    ClearUsdAttributes(uniformGeom.GetAngularVelocitiesAttr(), timeVarGeom.GetAngularVelocitiesAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::ANGULARVELOCITIES);

      // Angular velocities
      UsdAttribute angularVelocitiesAttribute = outGeom.GetAngularVelocitiesAttr();
      assert(angularVelocitiesAttribute);
      if (geomData.AngularVelocities)
      {
        GfVec3f* angVels = (GfVec3f*)geomData.AngularVelocities;

        VtVec3fArray& usdAngularVelocities = GetStaticTempArray<VtVec3fArray>();
        usdAngularVelocities.assign(angVels, angVels + geomData.NumPoints);
        angularVelocitiesAttribute.Set(usdAngularVelocities, timeCode);
      }
      else
      {
        angularVelocitiesAttribute.Set(SdfValueBlock(), timeCode);
      }
    }
  }

  template<typename UsdGeomType, typename GeomDataType>
  void UpdateUsdGeomInvisibleIds(UsdBridgeUsdWriter* writer, UsdGeomType& timeVarGeom, UsdGeomType& uniformGeom, const GeomDataType& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const GeomDataType>& updateEval, TimeEvaluator<GeomDataType>& timeEval)
  {
    using DMI = typename GeomDataType::DataMemberId;
    bool performsUpdate = updateEval.PerformsUpdate(DMI::INVISIBLEIDS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::INVISIBLEIDS);

    ClearUsdAttributes(uniformGeom.GetInvisibleIdsAttr(), timeVarGeom.GetInvisibleIdsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomType& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::INVISIBLEIDS);

      // Invisible ids
      UsdAttribute invisIdsAttr = outGeom.GetInvisibleIdsAttr();
      assert(invisIdsAttr);
      uint64_t numInvisibleIds = geomData.NumInvisibleIds;
      if (numInvisibleIds)
      {
        const void* arrayData = geomData.InvisibleIds;
        size_t arrayNumElements = numInvisibleIds;
        UsdAttribute arrayPrimvar = invisIdsAttr;
        switch (geomData.InvisibleIdsType)
        {
        case UsdBridgeType::UINT: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtInt64Array, unsigned int); break; }
        case UsdBridgeType::INT: {ASSIGN_PRIMVAR_CONVERT_MACRO(VtInt64Array, int); break; }
        case UsdBridgeType::LONG: {ASSIGN_PRIMVAR_MACRO(VtInt64Array); break; }
        case UsdBridgeType::ULONG: {ASSIGN_PRIMVAR_MACRO(VtInt64Array); break; }
        default: { UsdBridgeLogMacro(writer, UsdBridgeLogLevel::ERR, "UsdGeom GetInvisibleIdsAttr should be (U)LONG or (U)INT."); break; }
        }
      }
      else
      {
        invisIdsAttr.Set(SdfValueBlock(), timeCode);
      }
    }
  }

  static void UpdateUsdGeomCurveLengths(UsdBridgeUsdWriter* writer, UsdGeomBasisCurves& timeVarGeom, UsdGeomBasisCurves& uniformGeom, const UsdBridgeCurveData& geomData, uint64_t numPrims,
    UsdBridgeUpdateEvaluator<const UsdBridgeCurveData>& updateEval, TimeEvaluator<UsdBridgeCurveData>& timeEval)
  {
    using DMI = typename UsdBridgeCurveData::DataMemberId;
    // Fill geom prim and geometry layer with data.
    bool performsUpdate = updateEval.PerformsUpdate(DMI::CURVELENGTHS);
    bool timeVaryingUpdate = timeEval.IsTimeVarying(DMI::CURVELENGTHS);

    ClearUsdAttributes(uniformGeom.GetCurveVertexCountsAttr(), timeVarGeom.GetCurveVertexCountsAttr(), timeVaryingUpdate);

    if (performsUpdate)
    {
      UsdGeomBasisCurves& outGeom = timeVaryingUpdate ? timeVarGeom : uniformGeom;
      UsdTimeCode timeCode = timeEval.Eval(DMI::POINTS);

      UsdAttribute vertCountAttr = outGeom.GetCurveVertexCountsAttr();
      assert(vertCountAttr);

      const void* arrayData = geomData.CurveLengths;
      size_t arrayNumElements = geomData.NumCurveLengths;
      UsdAttribute arrayPrimvar = vertCountAttr;
      { ASSIGN_PRIMVAR_MACRO(VtIntArray); }
    }
  }
}

UsdPrim UsdBridgeUsdWriter::InitializeUsdGeometry(UsdStageRefPtr geometryStage, const SdfPath& geomPath, const UsdBridgeMeshData& meshData, bool uniformPrim) const
{
  return InitializeUsdGeometry_Impl(geometryStage, geomPath, meshData, uniformPrim, Settings);
}

UsdPrim UsdBridgeUsdWriter::InitializeUsdGeometry(UsdStageRefPtr geometryStage, const SdfPath& geomPath, const UsdBridgeInstancerData& instancerData, bool uniformPrim) const
{
  return InitializeUsdGeometry_Impl(geometryStage, geomPath, instancerData, uniformPrim, Settings);
}

UsdPrim UsdBridgeUsdWriter::InitializeUsdGeometry(UsdStageRefPtr geometryStage, const SdfPath& geomPath, const UsdBridgeCurveData& curveData, bool uniformPrim) const
{
  return InitializeUsdGeometry_Impl(geometryStage, geomPath, curveData, uniformPrim, Settings);
}

#ifdef VALUE_CLIP_RETIMING
void UsdBridgeUsdWriter::UpdateUsdGeometryManifest(const UsdBridgePrimCache* cacheEntry, const UsdBridgeMeshData& meshData)
{
  TimeEvaluator<UsdBridgeMeshData> timeEval(meshData);
  InitializeUsdGeometry_Impl(cacheEntry->ManifestStage.second, cacheEntry->PrimPath, meshData, false, 
    Settings, &timeEval);

  if(this->EnableSaving)
    cacheEntry->ManifestStage.second->Save();
}

void UsdBridgeUsdWriter::UpdateUsdGeometryManifest(const UsdBridgePrimCache* cacheEntry, const UsdBridgeInstancerData& instancerData)
{
  TimeEvaluator<UsdBridgeInstancerData> timeEval(instancerData);
  InitializeUsdGeometry_Impl(cacheEntry->ManifestStage.second, cacheEntry->PrimPath, instancerData, false, 
    Settings, &timeEval);

  if(this->EnableSaving)
    cacheEntry->ManifestStage.second->Save();
}

void UsdBridgeUsdWriter::UpdateUsdGeometryManifest(const UsdBridgePrimCache* cacheEntry, const UsdBridgeCurveData& curveData)
{
  TimeEvaluator<UsdBridgeCurveData> timeEval(curveData);
  InitializeUsdGeometry_Impl(cacheEntry->ManifestStage.second, cacheEntry->PrimPath, curveData, false, 
    Settings, &timeEval);

  if(this->EnableSaving)
    cacheEntry->ManifestStage.second->Save();
}
#endif

#define UPDATE_USDGEOM_ARRAYS(FuncDef) \
  FuncDef(this, timeVarGeom, uniformGeom, geomData, numPrims, updateEval, timeEval)

#define UPDATE_USDGEOM_PRIMVAR_ARRAYS(FuncDef) \
  FuncDef(this, timeVarPrimvars, uniformPrimvars, geomData, numPrims, updateEval, timeEval)

void UsdBridgeUsdWriter::UpdateUsdGeometry(const UsdStagePtr& timeVarStage, const SdfPath& meshPath, const UsdBridgeMeshData& geomData, double timeStep)
{
  // To avoid data duplication when using of clip stages, we need to potentially use the scenestage prim for time-uniform data.
  UsdGeomMesh uniformGeom = UsdGeomMesh::Get(this->SceneStage, meshPath);
  assert(uniformGeom);
  UsdGeomPrimvarsAPI uniformPrimvars(uniformGeom);

  UsdGeomMesh timeVarGeom = UsdGeomMesh::Get(timeVarStage, meshPath);
  assert(timeVarGeom);
  UsdGeomPrimvarsAPI timeVarPrimvars(timeVarGeom);

  // Update the mesh
  UsdBridgeUpdateEvaluator<const UsdBridgeMeshData> updateEval(geomData);
  TimeEvaluator<UsdBridgeMeshData> timeEval(geomData, timeStep);

  assert((geomData.NumIndices % geomData.FaceVertexCount) == 0);
  uint64_t numPrims = int(geomData.NumIndices) / geomData.FaceVertexCount;

  UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomPoints);
  UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomNormals);
  if( Settings.EnableStTexCoords && UsdGeomDataHasTexCoords(geomData) ) 
    { UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomTexCoords); }
  UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomAttributes);
  UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomColors);
  UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomIndices);
}

void UsdBridgeUsdWriter::UpdateUsdGeometry(const UsdStagePtr& timeVarStage, const SdfPath& instancerPath, const UsdBridgeInstancerData& geomData, double timeStep)
{
  UsdBridgeUpdateEvaluator<const UsdBridgeInstancerData> updateEval(geomData);
  TimeEvaluator<UsdBridgeInstancerData> timeEval(geomData, timeStep);

  bool useGeomPoints = UsesUsdGeomPoints(geomData);

  uint64_t numPrims = geomData.NumPoints;

  if (useGeomPoints)
  {
    UsdGeomPoints uniformGeom = UsdGeomPoints::Get(this->SceneStage, instancerPath);
    assert(uniformGeom);
    UsdGeomPrimvarsAPI uniformPrimvars(uniformGeom);

    UsdGeomPoints timeVarGeom = UsdGeomPoints::Get(timeVarStage, instancerPath);
    assert(timeVarGeom);
    UsdGeomPrimvarsAPI timeVarPrimvars(timeVarGeom);

    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomPoints);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomInstanceIds);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomWidths);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomOrientNormals);
    if( Settings.EnableStTexCoords && UsdGeomDataHasTexCoords(geomData) ) 
      { UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomTexCoords); }
    UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomAttributes);
    UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomColors);
  }
  else
  {
    UsdGeomPointInstancer uniformGeom = UsdGeomPointInstancer::Get(this->SceneStage, instancerPath);
    assert(uniformGeom);
    UsdGeomPrimvarsAPI uniformPrimvars(uniformGeom);

    UsdGeomPointInstancer timeVarGeom = UsdGeomPointInstancer::Get(timeVarStage, instancerPath);
    assert(timeVarGeom);
    UsdGeomPrimvarsAPI timeVarPrimvars(timeVarGeom);

    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomPoints);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomInstanceIds);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomScales);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomOrientations);
    if( Settings.EnableStTexCoords && UsdGeomDataHasTexCoords(geomData) ) 
      { UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomTexCoords); }
    UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomAttributes);
    UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomColors);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomShapeIndices);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomLinearVelocities);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomAngularVelocities);
    UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomInvisibleIds);
  }
}

void UsdBridgeUsdWriter::UpdateUsdGeometry(const UsdStagePtr& timeVarStage, const SdfPath& curvePath, const UsdBridgeCurveData& geomData, double timeStep)
{
  // To avoid data duplication when using of clip stages, we need to potentially use the scenestage prim for time-uniform data.
  UsdGeomBasisCurves uniformGeom = UsdGeomBasisCurves::Get(this->SceneStage, curvePath);
  assert(uniformGeom);
  UsdGeomPrimvarsAPI uniformPrimvars(uniformGeom);

  UsdGeomBasisCurves timeVarGeom = UsdGeomBasisCurves::Get(timeVarStage, curvePath);
  assert(timeVarGeom);
  UsdGeomPrimvarsAPI timeVarPrimvars(timeVarGeom);

  // Update the curve
  UsdBridgeUpdateEvaluator<const UsdBridgeCurveData> updateEval(geomData);
  TimeEvaluator<UsdBridgeCurveData> timeEval(geomData, timeStep);

  uint64_t numPrims = geomData.NumCurveLengths;

  UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomPoints);
  UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomNormals);
  if( Settings.EnableStTexCoords && UsdGeomDataHasTexCoords(geomData) ) 
    { UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomTexCoords); }
  UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomAttributes);
  UPDATE_USDGEOM_PRIMVAR_ARRAYS(UpdateUsdGeomColors);
  UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomWidths);
  UPDATE_USDGEOM_ARRAYS(UpdateUsdGeomCurveLengths);
}