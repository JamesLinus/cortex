//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2012-2016, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "ai.h"

#include "IECore/VectorTypedData.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/Primitive.h"
#include "IECore/MessageHandler.h"

#include "IECoreArnold/ParameterAlgo.h"
#include "IECoreArnold/ShapeAlgo.h"

using namespace std;
using namespace IECore;
using namespace IECoreArnold;

//////////////////////////////////////////////////////////////////////////
// Internal utilities
//////////////////////////////////////////////////////////////////////////

namespace
{

AtArray *identityIndices( size_t size )
{
	AtArray *result = AiArrayAllocate( size, 1, AI_TYPE_UINT );
	for( size_t i=0; i < size; ++i )
	{
		AiArraySetInt( result, i, i );
	}
	return result;
}

ConstFloatVectorDataPtr radius( const Primitive *primitive )
{
	if( ConstFloatVectorDataPtr radius = primitive->variableData<FloatVectorData>( "radius" ) )
	{
		return radius;
	}

	FloatVectorDataPtr calculatedRadius = new FloatVectorData();
	if( const FloatData *constantRadius = primitive->variableData<FloatData>( "radius", PrimitiveVariable::Constant ) )
	{
		calculatedRadius->writable().push_back( constantRadius->readable() );
	}
	else if( const FloatVectorData *width = primitive->variableData<FloatVectorData>( "width" ) )
	{
		calculatedRadius->writable().resize( width->readable().size() );
		const std::vector<float>::iterator end = calculatedRadius->writable().end();
		std::vector<float>::const_iterator wIt = width->readable().begin();
		for( std::vector<float>::iterator it = calculatedRadius->writable().begin(); it != end; it++, wIt++ )
		{
			*it = *wIt / 2.0f;
		}
	}
	else
	{
		const FloatData *constantWidth = primitive->variableData<FloatData>( "width", PrimitiveVariable::Constant );
		if( !constantWidth )
		{
			constantWidth = primitive->variableData<FloatData>( "constantwidth", PrimitiveVariable::Constant );
		}
		float r = constantWidth ? constantWidth->readable() / 2.0f : 0.5f;
		calculatedRadius->writable().push_back( r );
	}
	return calculatedRadius;
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// Implementation of public API.
//////////////////////////////////////////////////////////////////////////

namespace IECoreArnold
{

namespace ShapeAlgo
{

void convertP( const IECore::Primitive *primitive, AtNode *shape, const char *name )
{
	const V3fVectorData *p = primitive->variableData<V3fVectorData>( "P", PrimitiveVariable::Vertex );
	if( !p )
	{
		throw Exception( "Primitive does not have \"P\" primitive variable of interpolation type Vertex." );
	}

	AiNodeSetArray(
		shape,
		name,
		AiArrayConvert( p->readable().size(), 1, AI_TYPE_POINT, (void *)&( p->readable()[0] ) )
	);
}

void convertP( const std::vector<const IECore::Primitive *> &samples, AtNode *shape, const char *name )
{
	vector<const Data *> dataSamples;
	dataSamples.reserve( samples.size() );

	for( vector<const Primitive *>::const_iterator it = samples.begin(), eIt = samples.end(); it != eIt; ++it )
	{
		const V3fVectorData *p = (*it)->variableData<V3fVectorData>( "P", PrimitiveVariable::Vertex );
		if( !p )
		{
			throw Exception( "Primitive does not have \"P\" primitive variable of interpolation type Vertex." );
		}
		dataSamples.push_back( p );
	}

	AtArray *array = ParameterAlgo::dataToArray( dataSamples, AI_TYPE_POINT );
	AiNodeSetArray( shape, name, array );
}

void convertRadius( const IECore::Primitive *primitive, AtNode *shape )
{
	ConstFloatVectorDataPtr r = radius( primitive );

	AiNodeSetArray(
		shape,
		"radius",
		AiArrayConvert( r->readable().size(), 1, AI_TYPE_FLOAT, (void *)&( r->readable()[0] ) )
	);
}

void convertRadius( const std::vector<const IECore::Primitive *> &samples, AtNode *shape )
{
	vector<ConstFloatVectorDataPtr> radiusSamples; // for ownership
	vector<const Data *> dataSamples; // for passing to dataToArray()
	radiusSamples.reserve( samples.size() );
	dataSamples.reserve( samples.size() );

	for( vector<const Primitive *>::const_iterator it = samples.begin(), eIt = samples.end(); it != eIt; ++it )
	{
		ConstFloatVectorDataPtr r = radius( *it );
		radiusSamples.push_back( r );
		dataSamples.push_back( r.get() );
	}

	AtArray *array = ParameterAlgo::dataToArray( dataSamples, AI_TYPE_FLOAT );
	AiNodeSetArray( shape, "radius", array );
}

void convertPrimitiveVariable( const IECore::Primitive *primitive, const PrimitiveVariable &primitiveVariable, AtNode *shape, const char *name )
{

	// make sure the primitive variable doesn't clash with built-ins
	const AtNodeEntry *entry = AiNodeGetNodeEntry( shape );
	if ( AiNodeEntryLookUpParameter(	entry, name ) != NULL ){
		msg(
			Msg::Warning,
			"ShapeAlgo::convertPrimitiveVariable",
			boost::format( "Primitive variable \"%s\" will be ignored because it clashes with Arnold's built-in parameters" ) % name
		);
		return;
	}

	// Arnold has "constant", "uniform", "varying" and "indexed" interpolation,
	// whereas Cortex has Constant, Uniform, Varying, Vertex and FaceVarying.
	// The conversion between the two depends on the type of the primitive.

	std::string arnoldInterpolation;
	switch( primitiveVariable.interpolation )
	{
		case PrimitiveVariable::Constant :
			arnoldInterpolation = "constant";
			break;
		case PrimitiveVariable::Uniform :
			arnoldInterpolation = "uniform";
			break;
		case PrimitiveVariable::Varying :
			arnoldInterpolation = "varying";
			break;
		case PrimitiveVariable::FaceVarying :
			if( primitive->isInstanceOf( MeshPrimitiveTypeId ) )
			{
				arnoldInterpolation = "indexed";
				break;
			}
			// "indexed" data only makes sense for meshes - fall
			// through to Vertex case,
		case PrimitiveVariable::Vertex :
			// Arnold doesn't appear to have vertex storage, but
			// fortunately for many primitives it is equivalent to varying.
			// Unfortunately that is not the case for cubic CurvesPrimitives, so
			// we can not currently export per-vertex data for cubic curves.
			if( primitive->variableSize( primitiveVariable.interpolation ) == primitive->variableSize( PrimitiveVariable::Varying ) )
			{
				arnoldInterpolation = "varying";
			}
			break;
		default :
			break;
	}

	if( arnoldInterpolation == "" )
	{
		msg(
			Msg::Warning,
			"ShapeAlgo::convertPrimitiveVariable",
			boost::format( "Unable to create user parameter \"%s\" because primitive variable has unsupported interpolation" ) % name
		);
		return;
	}

	if( primitive->isInstanceOf( PointsPrimitiveTypeId ) )
	{
		// Cortex treats uniform as one-per-primitive
		// but Arnold treats uniform as one-per-point.
		if( arnoldInterpolation == "uniform" )
		{
			arnoldInterpolation = "constant";
		}
		else if( arnoldInterpolation == "varying" )
		{
			arnoldInterpolation = "uniform";
		}
	}

	// Deal with the simple case of constant data.

	if( arnoldInterpolation == "constant" )
	{
		ParameterAlgo::setParameter( shape, name, primitiveVariable.data.get() );
		return;
	}

	// Now deal with more complex cases with array data.

	bool isArray = false;
	int type = ParameterAlgo::parameterType( primitiveVariable.data.get(), isArray );
	if( type == AI_TYPE_NONE || !isArray )
	{
		msg(
			Msg::Warning,
			"ShapeAlgo::convertPrimitiveVariable",
			boost::format( "Unable to create user parameter \"%s\" for primitive variable of type \"%s\"" ) % name % primitiveVariable.data->typeName()
		);
		return;
	}

	std::string typeString = arnoldInterpolation + " " + AiParamGetTypeName( type );
	AiNodeDeclare( shape, name, typeString.c_str() );
	AtArray *array = ParameterAlgo::dataToArray( primitiveVariable.data.get(), type );
	if( array )
	{
		AiNodeSetArray( shape, name, array );
		if( arnoldInterpolation == "indexed" )
		{
			AiNodeSetArray(
				shape,
				(name + string("idxs")).c_str(),
				identityIndices( array->nelements )
			);
		}
	}
	else
	{
		msg(
			Msg::Warning,
			"ShapeAlgo::convertPrimitiveVariable",
			boost::format( "Failed to create array for parameter \"%s\" from data of type \"%s\"" ) % name % primitiveVariable.data->typeName()
		);
	}
}

void convertPrimitiveVariables( const IECore::Primitive *primitive, AtNode *shape, const char **namesToIgnore )
{
	for( PrimitiveVariableMap::const_iterator it = primitive->variables.begin(), eIt = primitive->variables.end(); it!=eIt; it++ )
	{
		if( namesToIgnore )
		{
			bool skip = false;
			for( const char **n = namesToIgnore; *n; n++ )
			{
				if( it->first == *n )
				{
					skip = true;
					break;
				}
			}
			if( skip )
			{
				continue;
			}
		}

		convertPrimitiveVariable( primitive, it->second, shape, it->first.c_str() );
	}
}

} // namespace ShapeAlgo

} // namespace IECoreArnold
