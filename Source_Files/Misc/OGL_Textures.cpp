/*
	
	OpenGL Texture Manager,
	by Loren Petrich,
	March 12, 2000

	This implements texture handling for OpenGL.
	
	May 2, 2000:
	
	Fixed silhouette-texture bug: color 0 is now transparent
	
	May 24, 2000:
	
	Added support for setting landscape aspect ratios from outside;
	also added more graceful degradation for mis-sized textures.
	Walls must be a power of 2 horizontally and vertical;
	landscapes must be a power of 2 horizontally
	in order for the tiling to work properly.
	
	June 11, 2000:
	
	Added support for opacity shift factor (OpacityShift alongside OpacityScale);
	should be good for making dark colors somewhat opaque.

Jul 10, 2000:

	Fixed crashing bug when OpenGL is inactive with ResetTextures()
*/

#include <GL/gl.h>
#include <GL/glu.h>
#include <agl.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include "cseries.h"
#include "interface.h"
#include "render.h"
#include "map.h"
#include "OGL_Setup.h"
#include "OGL_Render.h"
#include "OGL_Textures.h"


// Standard type assignments;
// this is done because of special concerns with rendering
// each of the different kinds of textures
const short TextureTypes[NUMBER_OF_COLLECTIONS] =
{
	NONE,					// Interface (what one sees in the HUD)
	OGL_Txtr_WeaponsInHand,	// Weapons in Hand
	
	OGL_Txtr_Inhabitant,	// Juggernaut
	OGL_Txtr_Inhabitant,	// Tick
	OGL_Txtr_Inhabitant,	// Explosion effects
	OGL_Txtr_Inhabitant,	// Hunter
	OGL_Txtr_Inhabitant,	// Player
	
	OGL_Txtr_Inhabitant,	// Items
	OGL_Txtr_Inhabitant,	// Trooper
	OGL_Txtr_Inhabitant,	// Fighter
	OGL_Txtr_Inhabitant,	// S'pht'Kr
	OGL_Txtr_Inhabitant,	// F'lickta
	
	OGL_Txtr_Inhabitant,	// Bob
	OGL_Txtr_Inhabitant,	// VacBob
	OGL_Txtr_Inhabitant,	// Enforcer
	OGL_Txtr_Inhabitant,	// Drone
	OGL_Txtr_Inhabitant,	// S'pht
	
	OGL_Txtr_Wall,			// Water
	OGL_Txtr_Wall,			// Lava
	OGL_Txtr_Wall,			// Sewage
	OGL_Txtr_Wall,			// Jjaro
	OGL_Txtr_Wall,			// Pfhor

	OGL_Txtr_Inhabitant,	// Water Scenery
	OGL_Txtr_Inhabitant,	// Lava Scenery
	OGL_Txtr_Inhabitant,	// Sewage Scenery
	OGL_Txtr_Inhabitant,	// Jjaro Scenery
	OGL_Txtr_Inhabitant,	// Pfhor Scenery
	
	OGL_Txtr_Landscape,		// Day
	OGL_Txtr_Landscape,		// Night
	OGL_Txtr_Landscape,		// Moon
	OGL_Txtr_Landscape,		// Outer Space
	
	OGL_Txtr_Inhabitant		// Cyborg
};


// Texture mapping
struct TxtrTypeInfoData
{
	GLenum NearFilter;			// OpenGL parameter for near filter (GL_NEAREST, etc.)
	GLenum FarFilter;			// OpenGL parameter for far filter (GL_NEAREST, etc.)
	int Resolution;				// 0 is full-sized, 1 is half-sized, 2 is fourth-sized
	GLenum ColorFormat;			// OpenGL parameter for stored color format (RGBA8, etc.)
};

static TxtrTypeInfoData TxtrTypeInfoList[OGL_NUMBER_OF_TEXTURE_TYPES];



// Infravision: use algorithm (red + green + blue)/3 to compose intensity,
// then shade with these colors, one color for each collection.

struct InfravisionData
{
	GLfloat Red, Green, Blue;	// Infravision tint components: 0 to 1
	bool IsTinted;				// whether to use infravision with this collection
};

struct InfravisionData IVDataList[NUMBER_OF_COLLECTIONS] =
{
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false},
	{1,1,1,false}
};

// Is infravision currently active?
static bool InfravisionActive = false;


// Allocate some textures and indicate whether an allocation had happened.
bool TextureState::Allocate()
{
	if (!IsUsed)
	{
		glGenTextures(NUMBER_OF_TEXTURES,IDs);
		IsUsed = true;
		return true;
	}
	return false;
}

// Use a texture and indicate whether to load it
bool TextureState::Use(int Which)
{
	glBindTexture(GL_TEXTURE_2D,IDs[Which]);
	if (!IDsInUse[Which])
	{
		IDsInUse[Which] = true;
		return true;
	}
	return false;
}


// Resets the object's texture state
void TextureState::Reset()
{
	if (IsUsed)
	{
		glDeleteTextures(NUMBER_OF_TEXTURES,IDs);
		IsUsed = IsGlowing = IDsInUse[Normal] = IDsInUse[Glowing] = false;
	}
}


static CollBitmapTextureState* TextureStateSets[MAXIMUM_COLLECTIONS];


// Initialize the texture accounting
void OGL_StartTextures()
{
	// Initialize the texture accounting proper
	for (int ic=0; ic<MAXIMUM_COLLECTIONS; ic++)
	{
		bool CollectionPresent = is_collection_present(ic);
		short NumberOfBitmaps =
			CollectionPresent ? get_number_of_collection_bitmaps(ic) : 0;
		TextureStateSets[ic] =
			(CollectionPresent && NumberOfBitmaps) ?
				(new CollBitmapTextureState[NumberOfBitmaps]) : 0;
	}
	
	// Initialize the texture-type info
	const int NUMBER_OF_NEAR_FILTERS = 2;
	const GLenum NearFilterList[NUMBER_OF_NEAR_FILTERS] =
	{
		GL_NEAREST,
		GL_LINEAR
	};
	const int NUMBER_OF_FAR_FILTERS = 6;
	const GLenum FarFilterList[NUMBER_OF_FAR_FILTERS] =
	{
		GL_NEAREST,
		GL_LINEAR,
		GL_NEAREST_MIPMAP_NEAREST,
		GL_LINEAR_MIPMAP_NEAREST,
		GL_NEAREST_MIPMAP_LINEAR,
		GL_LINEAR_MIPMAP_LINEAR
	};
	const int NUMBER_OF_COLOR_FORMATS = 3;
	const GLenum ColorFormatList[NUMBER_OF_COLOR_FORMATS] = 
	{
		GL_RGBA8,
		GL_RGB5_A1,
		GL_RGBA2
	};
	
	OGL_ConfigureData& ConfigureData = Get_OGL_ConfigureData();
	
	for (int k=0; k<OGL_NUMBER_OF_TEXTURE_TYPES; k++)
	{
		OGL_Texture_Configure& TxtrConfigure = ConfigureData.TxtrConfigList[k];
		TxtrTypeInfoData& TxtrTypeInfo = TxtrTypeInfoList[k];
		
		byte NearFilter = TxtrConfigure.NearFilter;
		if (NearFilter < NUMBER_OF_NEAR_FILTERS)
			TxtrTypeInfo.NearFilter = NearFilterList[NearFilter];
		else
			TxtrTypeInfo.NearFilter = GL_NEAREST;
		
		byte FarFilter = TxtrConfigure.FarFilter;
		if (FarFilter < NUMBER_OF_FAR_FILTERS)
			TxtrTypeInfo.FarFilter = FarFilterList[FarFilter];
		else
			TxtrTypeInfo.FarFilter = GL_NEAREST;
		
		TxtrTypeInfo.Resolution = TxtrConfigure.Resolution;
		
		byte ColorFormat = TxtrConfigure.ColorFormat;
		if (ColorFormat < NUMBER_OF_COLOR_FORMATS)
			TxtrTypeInfo.ColorFormat = ColorFormatList[ColorFormat];
		else
			TxtrTypeInfo.ColorFormat = GL_RGBA8;
	}
}


// Done with the texture accounting
void OGL_StopTextures()
{
	// Clear the texture accounting
	for (int ic=0; ic<MAXIMUM_COLLECTIONS; ic++)
		if (TextureStateSets[ic]) delete []TextureStateSets[ic];
}


// Find an OpenGL-friendly color table from a Marathon shading table
static void FindOGLColorTable(int NumSrcBytes, byte *OrigColorTable, GLuint *ColorTable)
{
	// Stretch the original color table to 4 bytes per value for OpenGL convenience;
	// all the intermediate calculations will be done in RGBA 8888 form,
	// because that is what OpenGL prefers as a texture input
	switch(NumSrcBytes)
	{
	case 2:
		for (int k=0; k<MAXIMUM_SHADING_TABLE_INDEXES; k++)
		{
			byte *OrigPtr = OrigColorTable + NumSrcBytes*k;
			GLuint &Color = ColorTable[k];
			
			// Convert from ARGB 5551 to RGBA 8888; make opaque
			GLushort Intmd = GLushort(OrigPtr[0]);
			Intmd = (Intmd << 8) | GLushort(OrigPtr[1]);
			Color = Convert_16to32(Intmd);
		}
		break;
		
	case 4:
		for (int k=0; k<MAXIMUM_SHADING_TABLE_INDEXES; k++)
		{
			byte *OrigPtr = OrigColorTable + NumSrcBytes*k;
			GLuint &Color = ColorTable[k];
			
			// Convert from ARGB 8888 to RGBA 8888; make opaque
			GLuint Chan;
			// Red
			Chan = OrigPtr[1];
			Color = Chan << 24;
			// Green
			Chan = OrigPtr[2];
			Color |= Chan << 16;
			// Blue
			Chan = OrigPtr[3];
			Color |= Chan << 8;
			// Alpha
			Color |= 0x000000ff;
		}
		break;
	}
}


inline bool IsLandscapeFlatColored()
{
	OGL_ConfigureData& ConfigureData = Get_OGL_ConfigureData();
	return (TEST_FLAG(ConfigureData.Flags,OGL_Flag_FlatLand) != 0);
}


static void MakeAverage(int Length, GLuint *Buffer)
{
	float Sum[4];
	
	for (int q=0; q<4; q++) Sum[q] = 0;
	
	// Extract the bytes; the lowest byte gets done first
	for (int k=0; k<Length; k++)
	{
		GLuint PixVal = Buffer[k];
		for (int q=0; q<4; q++)
		{
			Sum[q] += (PixVal & 0x000000ff);
			PixVal >>= 8;
		}
	}
	
	// This processes the bytes from highest to lowest
	GLuint AvgVal = 0;
	for (int q=0; q<4; q++)
	{
		AvgVal <<= 8;	// Must come before adding in a byte
		AvgVal |= PIN(int(Sum[3-q]/Length + 0.5),0,255);
	}

	for (int k=0; k<Length; k++)
		Buffer[k] = AvgVal;
}


/*
	Routine for using some texture; it will load the texture if necessary.
	It parses a shape descriptor and checks on whether the collection's texture type
	is one of those given.
	It will check for more than one intended texture type,
	a convenience for multiple texture types sharing the same handling.
	
	It uses the transfer mode and the transfer data to work out
	what transfer modes to use (invisibility is a special case of tinted)
*/
bool TextureManager::Setup(int TextureType0, int TextureType1)
{

	// Parse the shape descriptor and check on whether the texture type
	// is the texture's intended type
	short CollColor = GET_DESCRIPTOR_COLLECTION(ShapeDesc);
	Collection = GET_COLLECTION(CollColor);
	CTable = GET_COLLECTION_CLUT(CollColor);
	Frame = GET_DESCRIPTOR_SHAPE(ShapeDesc);
	Bitmap = get_bitmap_index(Collection,Frame);
	
	TextureType = TextureTypes[Collection];
	if (TextureType == NONE) return false;
	
	bool TextureOK = false;	
	if (TextureType == TextureType0) TextureOK = true;
	else if (TextureType == TextureType1) TextureOK = true;	
	if (!TextureOK) return false;
	
	// Tinted mode is only used for invisibility, and infravision will make objects visible
	if (TransferMode == _static_transfer) CTable = SILHOUETTE_BITMAP_SET;
	else if (TransferMode == _tinted_transfer) CTable = SILHOUETTE_BITMAP_SET;
	else if (InfravisionActive) CTable = INFRAVISION_BITMAP_SET;
	
	// Get the texture-state info: first, per-collection, then per-bitmap
	CollBitmapTextureState *CBTSList = TextureStateSets[Collection];
	if (CBTSList == NULL) return false;
	CollBitmapTextureState& CBTS = CBTSList[Bitmap];
	
	// Get the control info for this texture type:
	TxtrTypeInfoData& TxtrTypeInfo = TxtrTypeInfoList[TextureType];
	
	// Get the rendering options for this texture:
	TxtrOptsPtr = OGL_GetTextureOptions(Collection,CTable,Bitmap);
	
	// Get the texture-state info: per-color-table -- be sure to preserve this for later
	// Set the texture ID, and load the texture if necessary
	// If "Use()" is true, then load, otherwise, assume the texture is loaded and skip
	TxtrStatePtr = &CBTS.CTStates[CTable];
	TextureState &CTState = *TxtrStatePtr;
	if (!CTState.IsUsed)
	{
		// Initial sprite scale/offset
		U_Scale = V_Scale = 1;
		U_Offset = V_Offset = 0;
		
		if (!SetupTextureGeometry()) return false;
		
		// Store sprite scale/offset
		CBTS.U_Scale = U_Scale;
		CBTS.V_Scale = V_Scale;
		CBTS.U_Offset = U_Offset;
		CBTS.V_Offset = V_Offset;
		
		FindColorTables();		
		CTState.IsGlowing = IsGlowing;
		
		// Load the fake landscape if selected
		if (TextureType == OGL_Txtr_Landscape)
			if (IsLandscapeFlatColored())
				NormalBuffer = GetFakeLandscape();
		
		// If not, then load the expected textures
		if (!NormalBuffer)
			NormalBuffer = GetOGLTexture(NormalColorTable);
		if (IsGlowing)
			GlowBuffer = GetOGLTexture(GlowColorTable);
		
		// Display size: may be shrunk
		LoadedWidth = MAX(TxtrWidth >> TxtrTypeInfo.Resolution, 1);
		LoadedHeight = MAX(TxtrHeight >> TxtrTypeInfo.Resolution, 1);
		
		if (LoadedWidth != TxtrWidth || LoadedHeight != TxtrHeight)
		{
			// Shrink it
			GLuint *NewNormalBuffer = Shrink(NormalBuffer);
			delete []NormalBuffer;
			NormalBuffer = NewNormalBuffer;
			
			if (IsGlowing)
			{
				GLuint *NewGlowBuffer = Shrink(GlowBuffer);
				delete []GlowBuffer;
				GlowBuffer = NewGlowBuffer;
			}
		}
		
		// Kludge for making top and bottom look flat
		/*
		if (TextureType == OGL_Txtr_Landscape)
		{
			MakeAverage(LoadedWidth,NormalBuffer);
			MakeAverage(LoadedWidth,NormalBuffer+LoadedWidth*(LoadedHeight-1));
		}
		*/
	}
	else
	{
		// Get sprite scale/offset
		U_Scale = CBTS.U_Scale;
		V_Scale = CBTS.V_Scale;
		U_Offset = CBTS.U_Offset;
		V_Offset = CBTS.V_Offset;
		
		// Get glow state
		IsGlowing = CTState.IsGlowing;
	}
		
	// Done!!!
	return true;
}


// Next power of 2; since OpenGL prefers powers of 2, it is necessary to work out
// the next one up for each texture dimension.
inline int NextPowerOfTwo(int n)
{
	int p = 1;
	while(p < n) {p <<= 1;}
	return p;
}


bool TextureManager::SetupTextureGeometry()
{
	// How many rows (scanlines) and columns
	if (Texture->flags&_COLUMN_ORDER_BIT)
	{
		BaseTxtrWidth = Texture->height;
		BaseTxtrHeight = Texture->width;
	}
	else
	{
		BaseTxtrWidth = Texture->width;
		BaseTxtrHeight = Texture->height;
	}
	
	short RowBytes = Texture->bytes_per_row;
	if (RowBytes != NONE)
		if (BaseTxtrWidth != RowBytes) return false;
	
	// The default
	WidthOffset = HeightOffset = 0;
	
	switch(TextureType)
	{
	case OGL_Txtr_Wall:
		// For tiling to be possible, the width and height must be powers of 2
		TxtrWidth = BaseTxtrWidth;
		TxtrHeight = BaseTxtrHeight;
		if (TxtrWidth != NextPowerOfTwo(TxtrWidth)) return false;
		if (TxtrHeight != NextPowerOfTwo(TxtrHeight)) return false;
		break;
		
	case OGL_Txtr_Landscape:
		if (IsLandscapeFlatColored())
		{
			TxtrWidth = 128;
			TxtrHeight = 128;
		}
		else
		{
			// Width is horizontal direction here
			TxtrWidth = BaseTxtrWidth;
			if (TxtrWidth != NextPowerOfTwo(TxtrWidth)) return false;
			// Use the landscape height here
			TxtrHeight = (Landscape_AspRatExp >= 0) ?
				(TxtrWidth >> Landscape_AspRatExp) :
					(TxtrWidth << (-Landscape_AspRatExp));
			
			// Offsets
			WidthOffset = (TxtrWidth - BaseTxtrWidth) >> 1;
			HeightOffset = (TxtrHeight - BaseTxtrHeight) >> 1;
		}
		
		break;
		
	case OGL_Txtr_Inhabitant:
	case OGL_Txtr_WeaponsInHand:
		{			
			// The 2 here is so that there will be an empty border around a sprite,
			// so that the texture can be conveniently mipmapped.
			TxtrWidth = NextPowerOfTwo(BaseTxtrWidth+2);
			TxtrHeight = NextPowerOfTwo(BaseTxtrHeight+2);
			
			// This kludge no longer necessary
			/*
			TxtrWidth = MAX(TxtrWidth,128);
			TxtrHeight = MAX(TxtrHeight,128);
			*/
						
			// Offsets
			WidthOffset = (TxtrWidth - BaseTxtrWidth) >> 1;
			HeightOffset = (TxtrHeight - BaseTxtrHeight) >> 1;
			
			// We can calculate the scales and offsets here
			double TWidRecip = 1/double(TxtrWidth);
			double THtRecip = 1/double(TxtrHeight);
			U_Scale = TWidRecip*double(BaseTxtrWidth);
			U_Offset = TWidRecip*WidthOffset;
			V_Scale = THtRecip*double(BaseTxtrHeight);
			V_Offset = THtRecip*HeightOffset;
		}
		break;
	}
	
	// Success!
	return true;
}


// Float -> unsigned long for opacity (alpha channel) values:
inline unsigned long MakeUnsignedLong(float x) {return (unsigned long)(0xff*x + 0.5);}


void TextureManager::FindColorTables()
{
	// Default
	IsGlowing = false;
	
	// The silhouette case is easy
	if (CTable == SILHOUETTE_BITMAP_SET)
	{
		NormalColorTable[0] = 0;
		for (int k=1; k<MAXIMUM_SHADING_TABLE_INDEXES; k++)
			NormalColorTable[k] = 0xffffffff;
		return;
	}
	
	// Number of source bytes, for reading off of the shading table
	short NumSrcBytes = bit_depth/8;
	
	// Shadeless polygons use the first, instead of the last, shading table
	byte *OrigColorTable = ((byte *)ShadingTables);
	byte *OrigGlowColorTable = OrigColorTable;
	if (!IsShadeless) OrigColorTable +=
		NumSrcBytes*(number_of_shading_tables - 1)*MAXIMUM_SHADING_TABLE_INDEXES;
	
	// Find the normal color table
	FindOGLColorTable(NumSrcBytes,OrigColorTable,NormalColorTable);
	
	// Find the glow-map color table;
	// only inhabitants are glowmapped.
	// Also, it seems that only infravision textures are shadeless.
	if (!IsShadeless && (TextureType != OGL_Txtr_Landscape))
	{
		// Find the glow table from the lowest-illumination color table
		FindOGLColorTable(NumSrcBytes,OrigGlowColorTable,GlowColorTable);
			
		// Search for self-luminous colors; ignore the first one as the transparent one
		for (int k=1; k<MAXIMUM_SHADING_TABLE_INDEXES; k++)
		{
			// Check for illumination-independent colors
			if (GlowColorTable[k] & 0xf0f0f000)
			{
				IsGlowing = true;
				// Make half-opaque, and the original color;
				// this is to get more like the software rendering
				GlowColorTable[k] = NormalColorTable[k] & 0xffffff80;
			}
			else
				// Make transparent but the original color,
				// so as to get appropriate continuity
				GlowColorTable[k] = NormalColorTable[k] & 0xffffff00;
		}
	}
	
	// Find the modified opacity if specified as a rendering option:
	short OpacityType = TxtrOptsPtr->OpacityType;
	if ((TextureType != OGL_Txtr_Landscape) && (OpacityType != OGL_OpacType_Crisp))
	{
		for (int k=1; k<MAXIMUM_SHADING_TABLE_INDEXES; k++)
		{
			// Get the normal color; the glow color is calculated from it,
			// so this is OK.
			unsigned long CTabEntry = NormalColorTable[k];
			if (!(CTabEntry & 0x000000ff)) continue;
			
			// Suppress the opacity, since we'll be replacing it
			CTabEntry &= 0xffffff00;
			
			// Find the overall opacity
			float Opacity = 1;
			if (OpacityType == OGL_OpacType_Avg)
			{
				unsigned long Red = (CTabEntry >> 24) & 0x000000ff;
				unsigned long Green = (CTabEntry >> 16) & 0x000000ff;
				unsigned long Blue = (CTabEntry >> 8) & 0x000000ff;
				Opacity = (Red + Green + Blue)/3.0/float(0xff);
			}
			else if (OpacityType == OGL_OpacType_Max)
			{
				unsigned long Red = (CTabEntry >> 24) & 0x000000ff;
				unsigned long Green = (CTabEntry >> 16) & 0x000000ff;
				unsigned long Blue = (CTabEntry >> 8) & 0x000000ff;
				Opacity = MAX(MAX(Red,Green),Blue)/float(0xff);
			}
			Opacity *= TxtrOptsPtr->OpacityScale;
			Opacity += TxtrOptsPtr->OpacityShift;
			Opacity = PIN(Opacity,0,1);
			
			// Replace only the really-glowing colors' opacities
			unsigned long GlowCTabEntry = GlowColorTable[k];
			if (IsGlowing && (GlowCTabEntry & 0x000000ff))
			{
				// Suppress the opacity, since we'll be replacing it
				GlowCTabEntry &= 0xffffff00;
				
				// The opacity values for each layer are selected to get
				// the appropriate overall effect (1/2 normal + 1/2 glowing)		
				GlowColorTable[k] = GlowCTabEntry | MakeUnsignedLong(Opacity/2);
				NormalColorTable[k] = CTabEntry | MakeUnsignedLong(Opacity/(2-Opacity));
			} else {
				// Non-glowing colors are done here
				NormalColorTable[k] = CTabEntry | MakeUnsignedLong(Opacity);
			}
		}
	}
	
	// The first color is always the transparent color,
	// except if it is a landscape color
	if (TextureType != OGL_Txtr_Landscape)
		{NormalColorTable[0] = 0; GlowColorTable[0] = 0;}	
}


GLuint *TextureManager::GetOGLTexture(GLuint *ColorTable)
{
	// Allocate and set to black and transparent
	int NumPixels = int(TxtrWidth)*int(TxtrHeight);
	GLuint *Buffer = new GLuint[NumPixels];
	memset(Buffer,0,NumPixels*sizeof(GLuint));
	
	// The dimension, the offset in the original texture, and the offset in the OpenGL texture
	short Width, OrigWidthOffset, OGLWidthOffset;
	short Height, OrigHeightOffset, OGLHeightOffset;
	
	// Calculate original-texture and OpenGL-texture offsets
	// and how many scanlines to do.
	// The loop start points and counts are set so that
	// only valid source and destination pixels
	// will get worked with (no off-edge ones, that is).
	if (HeightOffset >= 0)
	{
		Height = BaseTxtrHeight;
		OrigHeightOffset = 0;
		OGLHeightOffset = HeightOffset;
	}
	else
	{
		Height = TxtrHeight;
		OrigHeightOffset = - HeightOffset;
		OGLHeightOffset = 0;
	}

	if (Texture->bytes_per_row == NONE)
	{
		short horig = OrigHeightOffset;
		GLuint *OGLRowStart = Buffer + TxtrWidth*OGLHeightOffset;
		for (short h=0; h<Height; h++)
		{
			byte *OrigStrip = Texture->row_addresses[horig];
			GLuint *OGLStrip = OGLRowStart;

			// Cribbed from textures.c:
			// This is the Marathon 2 sprite-interpretation scheme;
			// assumes big-endian data
				
			// First destination location
			word First = word(*(OrigStrip++));
			First <<= 8;
			First |= word(*(OrigStrip++));
			// Last destination location (last pixel is just before it)
			word Last = word(*(OrigStrip++));
			Last <<= 8;
			Last |= word(*(OrigStrip++));
			
			// Calculate original-texture and OpenGL-texture offsets
			// and how many pixels to do
			OrigWidthOffset = 0;
			OGLWidthOffset = WidthOffset + First;
			
			if (OGLWidthOffset < 0)
			{
				OrigWidthOffset -= OGLWidthOffset;
				OGLWidthOffset = 0;
			}
			
			short OrigWidthFinish = Last - First;
			short OGLWidthFinish = WidthOffset + Last;
			
			short OGLWidthExcess = OGLWidthFinish - TxtrWidth;
			if (OGLWidthExcess > 0)
			{
				OrigWidthFinish -= OGLWidthExcess;
				OGLWidthFinish = TxtrWidth;
			}
			
			short Width = OrigWidthFinish - OrigWidthOffset;
			OrigStrip += OrigWidthOffset;
			OGLStrip += OGLWidthOffset;
			
			for (short w=0; w<Width; w++)
				*(OGLStrip++) = ColorTable[*(OrigStrip++)];
			horig++;
			OGLRowStart += TxtrWidth;
		}
	}
	else
	{
		// Calculate original-texture and OpenGL-texture offsets
		// and how many pixels to do
		if (WidthOffset >= 0)
		{
			Width = BaseTxtrWidth;
			OrigWidthOffset = 0;
			OGLWidthOffset = WidthOffset;
		}
		else
		{
			Width = TxtrWidth;
			OrigWidthOffset = - WidthOffset;
			OGLWidthOffset = 0;
		}
		short horig = OrigHeightOffset;
		GLuint *OGLRowStart = Buffer + TxtrWidth*OGLHeightOffset + OGLWidthOffset;
		for (short h=0; h<Height; h++)
		{
			byte *OrigStrip = Texture->row_addresses[horig] + OrigWidthOffset;
			GLuint *OGLStrip = OGLRowStart;
			for (short w=0; w<Width; w++)
				*(OGLStrip++) = ColorTable[*(OrigStrip++)];
			horig++;
			OGLRowStart += TxtrWidth;
		}
	}
	
	return Buffer;
}


inline GLuint MakeEightBit(GLfloat Chan)
{
	return GLuint(PIN(GLint(255*Chan+0.5),0,255));
}

GLuint MakeTxtrColor(GLfloat *Color)
{
	GLuint Red = MakeEightBit(Color[0]) << 24;
	GLuint Green = MakeEightBit(Color[1]) << 16;
	GLuint Blue = MakeEightBit(Color[2]) << 8;
	GLuint Alpha = MakeEightBit(Color[3]);

	return (Red | Green | Blue | Alpha); 
}


GLuint *TextureManager::GetFakeLandscape()
{
	// Allocate and set to black and transparent
	int NumPixels = int(TxtrWidth)*int(TxtrHeight);
	GLuint *Buffer = new GLuint[NumPixels];
	memset(Buffer,0,NumPixels*sizeof(GLuint));
	
	// Set up land and sky colors:
	OGL_ConfigureData& ConfigureData = Get_OGL_ConfigureData();
	RGBColor OrigLandColor = ConfigureData.LscpColors[static_world->song_index][0];
	RGBColor OrigSkyColor = ConfigureData.LscpColors[static_world->song_index][1];
	
	// Set up floating-point ones, complete with alpha channel
	GLfloat LandColor[4], SkyColor[4];
	MakeFloatColor(OrigLandColor,LandColor);
	LandColor[3] = 1;
	MakeFloatColor(OrigSkyColor,SkyColor);
	SkyColor[3] = 1;
	
	// Modify if infravision is active
	if (CTable == INFRAVISION_BITMAP_SET)
	{
		FindInfravisionVersion(Collection,LandColor);
		FindInfravisionVersion(Collection,SkyColor);
	}
	
	GLuint TxtrLandColor = MakeTxtrColor(LandColor);
	GLuint TxtrSkyColor = MakeTxtrColor(SkyColor);
	
	// Textures' vertical dimension is upward;
	// put in the land after the sky
	GLuint *BufPtr = Buffer;
	for (int h=0; h<TxtrHeight/2; h++)
		for (int w=0; w<TxtrWidth; w++)
			*(BufPtr++) = TxtrLandColor;
	for (int h=0; h<TxtrHeight/2; h++)
		for (int w=0; w<TxtrWidth; w++)
			*(BufPtr++) = TxtrSkyColor;
		
	return Buffer;
}


GLuint *TextureManager::Shrink(GLuint *Buffer)
{
	int NumPixels = int(LoadedWidth)*int(LoadedHeight);
	GLuint *NewBuffer = new GLuint[NumPixels];
	gluScaleImage(GL_RGBA, TxtrWidth, TxtrHeight, GL_UNSIGNED_BYTE, Buffer,
		LoadedWidth, LoadedHeight, GL_UNSIGNED_BYTE, NewBuffer);
	
	return NewBuffer;
}


// This places a texture into the OpenGL software and gives it the right
// mapping attributes
void TextureManager::PlaceTexture(bool IsOverlaid, GLuint *Buffer)
{

	TxtrTypeInfoData& TxtrTypeInfo = TxtrTypeInfoList[TextureType];

	// Load the texture
	switch(TxtrTypeInfo.FarFilter)
	{
	case GL_NEAREST:
	case GL_LINEAR:
		glTexImage2D(GL_TEXTURE_2D, 0, TxtrTypeInfo.ColorFormat, LoadedWidth, LoadedHeight,
			0, GL_RGBA, GL_UNSIGNED_BYTE, Buffer);
		break;
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_LINEAR_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
	case GL_LINEAR_MIPMAP_LINEAR:
		gluBuild2DMipmaps(GL_TEXTURE_2D, TxtrTypeInfo.ColorFormat, LoadedWidth, LoadedHeight,
			GL_RGBA, GL_UNSIGNED_BYTE, Buffer);
		break;
	
	default:
		// Shouldn't happen
		assert(false);
	}
	
	// Set texture-mapping features
	if (IsOverlaid)
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
	else
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, TxtrTypeInfo.NearFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, TxtrTypeInfo.FarFilter);
	
	switch(TextureType)
	{
	case OGL_Txtr_Wall:
		// Walls are tiled in both direction
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		break;
		
	case OGL_Txtr_Landscape:
		// Landscapes repeat horizontally, have vertical limits
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		break;
		
	case OGL_Txtr_Inhabitant:
	case OGL_Txtr_WeaponsInHand:
		// Sprites have both horizontal and vertical limits
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		break;
	}
}


// What to render:

// Always call this one and call it first; safe to allocate texture ID's in it
void TextureManager::RenderNormal()
{
	TxtrStatePtr->Allocate();
	
	if (TxtrStatePtr->UseNormal())
	{
		assert(NormalBuffer);
		PlaceTexture(false,NormalBuffer);
	}
}

// Call this one after RenderNormal()
void TextureManager::RenderGlowing(bool IsOverlaid)
{
	if (TxtrStatePtr->UseGlowing())
	{
		assert(GlowBuffer);
		PlaceTexture(IsOverlaid,GlowBuffer);
	}
}


// Init
TextureManager::TextureManager()
{
	NormalBuffer = 0;
	GlowBuffer = 0;
	
	TxtrStatePtr = 0;
	TxtrOptsPtr = 0;
	
	// Marathon default
	Landscape_AspRatExp = 1;
}

// Cleanup
TextureManager::~TextureManager()
{
	if (NormalBuffer != 0) delete []NormalBuffer;
	if (GlowBuffer != 0) delete []GlowBuffer;
}

void OGL_ResetTextures()
{
	// Fix for crashing bug when OpenGL is inactive
	if (!OGL_IsActive()) return;
	
	// Reset the textures:
	for (int ic=0; ic<MAXIMUM_COLLECTIONS; ic++)
	{
		bool CollectionPresent = is_collection_present(ic);
		short NumberOfBitmaps =
			CollectionPresent ? get_number_of_collection_bitmaps(ic) : 0;
		
		CollBitmapTextureState *CBTSSet = TextureStateSets[ic];
		for (int ib=0; ib<NumberOfBitmaps; ib++)
		{
			TextureState *TSSet = CBTSSet[ib].CTStates;
			for (int ist=0; ist<NUMBER_OF_OPENGL_BITMAP_SETS; ist++)
				TSSet[ist].Reset();
		}
	}
}


// Infravision (I'm blue, are you?)
bool& IsInfravisionActive() {return InfravisionActive;}


// Sets the infravision tinting color for a shapes collection, and whether to use such tinting;
// the color values are from 0 to 1.
bool SetInfravisionTint(short Collection, bool IsTinted, float Red, float Green, float Blue)
{	
	assert(Collection >= 0 && Collection < NUMBER_OF_COLLECTIONS);
	InfravisionData& IVData = IVDataList[Collection];
	
	IVData.Red = Red;
	IVData.Green = Green;
	IVData.Blue = Blue;
	IVData.IsTinted = IsTinted;
	
	return true;
}


// Finds the infravision version of a color for some collection set;
// it makes no change if infravision is inactive.
void FindInfravisionVersion(short Collection, GLfloat *Color)
{
	if (InfravisionActive)
	{
		InfravisionData& IVData = IVDataList[Collection];
		if (IVData.IsTinted)
		{
			GLfloat AvgColor = (Color[0] + Color[1] + Color[2])/3;
			Color[0] = IVData.Red*AvgColor;
			Color[1] = IVData.Green*AvgColor;
			Color[2] = IVData.Blue*AvgColor;
		}
	}
}

/*
// Stuff for doing 16->32 pixel-format conversion, 1555 ARGB to 8888 RGBA
GLuint *ConversionTable_16to32 = NULL;

void MakeConversion_16to32(int BitDepth)
{
	// This is for allocating a 16->32 conversion table only when necessary
	if (BitDepth == 16 && (!ConversionTable_16to32))
	{
		// Allocate it
		int TableSize = (1 << 15);
		ConversionTable_16to32 = new GLuint[TableSize];
	
		// Fill it
		for (word InVal = 0; InVal < TableSize; InVal++)
			ConversionTable_16to32[InVal] = Convert_16to32(InVal);
	}
	else if (ConversionTable_16to32)
	{
		// Get rid of it
		delete []ConversionTable_16to32;
		ConversionTable_16to32 = NULL;
	}
}
*/
