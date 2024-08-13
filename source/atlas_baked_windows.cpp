// Tedious Typography
//
// Font sizes can be specified in points (pt), 1 pt = 1/72 inch (0.35 mm).
//
// The font size (height) is broken up into the ascent and descent.
// Ascent is the space above the baseline.
// Descent is the space below the baseline.
//
// Font size = Ascent + Descent
// e.g A 72pt font may have an ascent of 56pt and descent of 16pt, thus 72pt = (56pt + 16pt)
//
// Font sizes can also be specified in logical units,
// 1 logical unit = 72pt (1 inch, 2.54 cm).
// 1 logical unit = DPI (px) (historically 96 px, but can be set on a user by user basis)
// e.g DPI setting of 100% (96 px), DPI setting of 125% (120 px), DPI setting of 150% (144 px)
//
// Font sizes can also be specified in device independent pixels (DIPs), 1 DIP = 1/96 logical unit.
// Therefore,
// 1 inch = 72 pt = 1 logical unit = 96 px (DPI, 100%)  = 1 DIP
// 1 inch = 72 pt = 1 logical unit = 144 px (DPI, 150%) = 1.5 DIP
//
// to get the font size in pixels given points:
// pixels = (points/72)*96*(DPI/100)
// 
// Windows peculiarities
// Something to note about Windows 'SetProcessDpiAwareness()'.
// If a program does not set its DPI awareness, then Windows scales the window (among other things) when the users DPI setting is not set to 100%.
// e.g A DPI unaware program specifies a window size of 500x500 on a machine that has a DPI setting of 150%.
//     Windows will scale the window to 750x750 (DWM scaling).
//

#include <windows.h>
#include <shellscalingapi.h>

#include "p:/shared/shared.cpp"
#include "p:/shared/shared_utils.cpp"
#include "p:/shared/shared_windows.cpp"
#include "p:/shared/shared_math.cpp"
#include "p:/shared/shared_string.cpp"

#include <cstdio>

global b32 running;
global s32 window_width  = 1280;
global s32 window_height = 720;

global b32 cmd_mode;

global s8   open_file[MAX_PATH] = { };
global s8   save_file[MAX_PATH] = { };
global s8 bitmap_file[MAX_PATH] = { };

global s8 fontheight_field[3] = { };

global u32 DPI;

#pragma pack(push, 1)
// bitmap.
struct bitmap_header
{
    u16   signature; // must be 'BM' (0x4d42)
    u32   file_size;
    u16  reserved_0; // must be 0
    u16  reserved_1; // must be 0
    u32 byte_offset; // offset into the file the actual pixel array begins (must be 122)

    u32 header_size; // sizeof(BITMAPINFOHEADER)
    s32       width;  
    s32      height; // positive (bottom-up DIB)
    
    u16 planes;              // must be 1
    u16 bits_per_pixel;      // must be 32    
    u32 compression;         // must be BI_BITFIELDS
    u32 image_size;          // must be 0
    s32 x_pixels_per_meter;  // must be 0 (no preference)
    s32 y_pixels_per_meter;  // must be 0 (no preference)
    u32        used_colours; // must be 0
    u32 significant_colours; // must be 0
};
// ttf.
#define TTF_SWAPWORD(x) MAKEWORD(HIBYTE(x), LOBYTE(x))
#define TTF_SWAPLONG(x) MAKELONG(TTF_SWAPWORD(HIWORD(x)), TTF_SWAPWORD(LOWORD(x)))
struct ttf_offsettable_header
{
    u16 major_version;
    u16 minor_version;
    u16 num_of_tables;
    u16 uSearchRange;
    u16 uEntrySelector;
    u16 uRangeShift;
};
struct ttf_directorytable_header
{
    s8 table_name[4]; 
    u32 checksum; 
    u32 offset; 
    u32 length; 
};
struct ttf_nametable_header
{
    u16 format_selector; // must be 0
    u16 namerecords_count; 
    u16 storage_offset;
};
struct ttf_name_header
{
    u16 platform_id;
    u16 encoding_id;
    u16 language_id;
    u16 name_id;
    u16 string_length;
    u16 string_offset;
};
// font.
#define GLYPH_COUNT   233
#define GLYPH_ROWS    16
#define GLYPH_COLUMNS 16
struct glyph_header
{
    s8  character;
    s32 offset;
    
    s32     spacing;
    s32 pre_spacing;
    
    s32  width;
    s32 height;

    r32 u0;
    r32 u1;
    r32 v0;
    r32 v1;
};
struct font_header
{
    s32   size;
    s32  width;
    s32 height;
    s32 glyph_count;

    s32 glyph_height;
    s32 glyph_width;

    s32 line_spacing;

    s32 glyph_offset;
    s32  byte_offset;
    
    glyph_header glyphs[GLYPH_COUNT];
};
#pragma pack(pop)

// bitmap.
internal void
bitmap_saveas(s8* bitmap_file, s32 bitmap_width, s32 bitmap_height, s8* bitmap_data)
{
    s32 bitmap_size = bitmap_width * bitmap_height * 4;

    bitmap_header header = {};
    header.signature      = 0x4D42; 
    header.file_size      = sizeof(bitmap_header) + bitmap_size;
    header.byte_offset    = sizeof(bitmap_header); 
    header.header_size    = sizeof(BITMAPINFOHEADER); 
    header.width          = bitmap_width;  
    header.height         = bitmap_height; 
    header.planes         = 1;            
    header.bits_per_pixel = 32;      
    header.compression    = BI_RGB;
    header.image_size     = bitmap_size;

    s8* save = (s8*)VirtualAlloc(0, header.file_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    mem_copy(&header, save, header.header_size);
    mem_copy(bitmap_data, save + header.byte_offset, bitmap_size);

    io_writefile(bitmap_file, header.file_size, save);

    VirtualFree(save, 0, MEM_RELEASE);
}
// ttf.
internal b32
ttf_fontfamily(s8* font_file, s8* font_family)
{
    io_file font = io_readfile(font_file);
    if(font.source)
    {
	ttf_offsettable_header* offset_table = (ttf_offsettable_header*)font.source;
	offset_table->num_of_tables = TTF_SWAPWORD(offset_table->num_of_tables);

	b32 table_found = false;

	ttf_directorytable_header* directory_table =
	(ttf_directorytable_header*)((s8*)font.source + sizeof(ttf_offsettable_header));
	for(s32 table = 0; table < offset_table->num_of_tables; table++)
	{
	    if(directory_table->table_name[0] == 'n' &&
	       directory_table->table_name[1] == 'a' &&
	       directory_table->table_name[2] == 'm' &&
	       directory_table->table_name[3] == 'e')
	    {
		table_found = true;
		directory_table->length = TTF_SWAPLONG(directory_table->length);
		directory_table->offset = TTF_SWAPLONG(directory_table->offset);
		break;
	    }
	    directory_table++;
	}

	if(table_found)
	{
	    ttf_nametable_header* name_table = (ttf_nametable_header*)((s8*)font.source + directory_table->offset);

	    name_table->namerecords_count = TTF_SWAPWORD(name_table->namerecords_count);
	    name_table->storage_offset    = TTF_SWAPWORD(name_table->storage_offset);

	    ttf_name_header* name_header = (ttf_name_header*)((s8*)name_table + sizeof(ttf_nametable_header));
	    for(s32 record = 0; record < name_table->namerecords_count; record++)
	    {
		name_header->name_id = TTF_SWAPWORD(name_header->name_id);
		if(name_header->name_id == 1) // font family
		{
		    name_header->string_length = TTF_SWAPWORD(name_header->string_length);
		    name_header->string_offset = TTF_SWAPWORD(name_header->string_offset);

		    // note, we can not to a simple 'mem_copy' as we require a null terminated string.
		    //       it would appear the spaces between letters are '\0'

		    s8* start = (s8*)font.source + directory_table->offset + name_table->storage_offset + 
		    name_header->string_offset + 1;
		    for(s32 c = 0; c < name_header->string_length - 1; c++)
		    {
			if(start[c] != '\0')
			{
			    *font_family++ = start[c];
			}
		    }
		    
		    io_freefile(font);
		    return(true);
		}
		name_header++;
	    }
	}
	
    }
    return(false);
}
// bake.
internal void
bake_writeglyph(void* source, u32 source_size_x, u32 source_size_y, u32 source_width,
		void* target, u32 target_width)
{
    for(u32 sy = 0; sy < source_size_y; sy++)
    {
	mem_copy(source, target, (source_size_x * 4));
	target = (u32*)target + target_width;
	source = (u32*)source + source_width;
    }
}
internal u32*
bake_loadglyph(HDC device_context, void* bytes,
	       s32 font_height, // in pixels.
	       s32* offset,
	       s32* glyph_width,
	       s32* glyph_height)
{
    ASSERT(bytes);

    // text extent?
    s32  subsection_width  = font_height * 2;
    s32  subsection_height = font_height * 2;
    u32* subsection        = (u32*)VirtualAlloc(0, (subsection_width * subsection_height * 4), MEM_COMMIT, PAGE_READWRITE);

    // copy and calculate bounds.
    s32 max_column = 0;
    s32 min_column = subsection_width;
    s32 max_row    = 0;
    s32 min_row    = subsection_height;

    u32* dib_memory = (u32*)bytes;
    u32* ptr = subsection;
    for(s32 y = 0; y < subsection_height; y++)  
    {
	u32* px = dib_memory;
	for(s32 x = 0; x < subsection_width; x++) 
	{
	    u8 a = *px++ & 0xff; 
	    if(a)
	    {
		if(x < min_column) min_column = x;
		if(x > max_column) max_column = x;
		if(y < min_row)    min_row    = y;
		if(y > max_row)    max_row    = y;
	    }
	    *ptr++ = a | (a << 8) | (a << 16) | (a << 24);
	}
	dib_memory += (font_height * 2);
    }
	    
    *glyph_width  = (max_column != 0) ? ((max_column - min_column) + 1) : 0;
    *glyph_height = (max_row    != 0) ? ((max_row    - min_row   ) + 1) : 0;

    // this is a save guard, shouldn't actually happen in practice! change to 'ASSERT'
    *glyph_width  = (*glyph_width  > font_height) ? font_height : *glyph_width;
    *glyph_height = (*glyph_height > font_height) ? font_height : *glyph_height;

    // smallest possible glyph.
    u32* glyph = (u32*)VirtualAlloc(0, *glyph_width * *glyph_height * 4, MEM_COMMIT, PAGE_READWRITE);
    if(glyph)
    {
	u32* subsection_ptr = subsection;
	subsection_ptr += (min_row * subsection_width) + min_column;

	bake_writeglyph(subsection_ptr, *glyph_width, *glyph_height, subsection_width, glyph, *glyph_width);
	
	VirtualFree(subsection, 0, MEM_RELEASE);
    }
    // free happens later.

    TEXTMETRICA metrics = {};
    GetTextMetricsA(device_context, &metrics);
    *offset = max_row - (subsection_height - metrics.tmAscent);

    return((u32*)glyph); 
}
internal b32
bake_loadfont(font_header* atlas, r32 points, s32 pixels, s8* font_file, u32** glyphs)
{
    b32 success = false;

    AddFontResourceExA(font_file, FR_PRIVATE, 0);

    s8 font_family[256] = {};
    ttf_fontfamily(font_file, font_family);

    HFONT font_handle = CreateFontA(-MulDiv((points/2.0), GetDeviceCaps(GetDC(0), LOGPIXELSY), 72), 0, 0, 0,
				    FW_NORMAL,   // weight
				    FALSE,       // italic
				    FALSE,       // underline
				    FALSE,       // strikeout
				    DEFAULT_CHARSET, 
				    OUT_DEFAULT_PRECIS,
				    CLIP_DEFAULT_PRECIS, 
				    ANTIALIASED_QUALITY,
				    DEFAULT_PITCH | FF_DONTCARE,
				    font_family);

    if(font_handle)
    {
	HDC device_context = CreateCompatibleDC(GetDC(0));

	if(device_context)
	{
	    SetMapMode(device_context, MM_TEXT);
	    
	    BITMAPINFO bitmap_info              = {};
	    bitmap_info.bmiHeader.biSize        =  sizeof(bitmap_info.bmiHeader);
	    bitmap_info.bmiHeader.biWidth       =  pixels*2;
	    bitmap_info.bmiHeader.biHeight      =  pixels*2; // (+) bottom-up, (-) top-down
	    bitmap_info.bmiHeader.biPlanes      =  1;
	    bitmap_info.bmiHeader.biBitCount    =  32;
	    bitmap_info.bmiHeader.biCompression =  BI_RGB;

	    s32 max_offset = 0;
	    s32 c = 0;
	    for(s32 g = 32; g < 256; g++) // '!'(32) -> 'ÿ'(255) 
	    {
		void*   bytes = 0;
		HBITMAP bitmap_handle = CreateDIBSection(device_context, &bitmap_info, DIB_RGB_COLORS, &bytes, 0, 0);
		if(bitmap_handle)
		{
		    // standard.
		    SelectObject (device_context, bitmap_handle);
		    SelectObject (device_context, font_handle);
		    SetBkColor   (device_context, RGB(  0,   0,   0));
		    SetTextColor (device_context, RGB(255, 255, 255));
		    TextOutA(device_context, 0, 0, (LPCSTR)&g, 1); // character output.

		    // ?
		    // SIZE size;
		    // GetTextExtentPoint32A(device_context, &character, 1, &size);
    
		    glyphs[c] = bake_loadglyph(device_context, bytes, pixels,
					       &atlas->glyphs[c].offset,
					       &atlas->glyphs[c].width,
					       &atlas->glyphs[c].height);
		    
		    if(atlas->glyphs[c].offset > max_offset) { max_offset = atlas->glyphs[c].offset; }

		    ABC character_metrics = {};
		    GetCharABCWidthsA(device_context, (u32)g, (u32)g, &character_metrics);
		    atlas->glyphs[c].character   = g;
		    atlas->glyphs[c].    spacing = character_metrics.abcC;
		    atlas->glyphs[c].pre_spacing = character_metrics.abcA;

		    DeleteObject(bitmap_handle);
		}
		else
		{
		    OutputDebugStringA("'CreateDIBSection' failed!\n");
		}
		c++;
	    }
	    for(u32 i = 0; i < GLYPH_COUNT; i++)
	    {
		atlas->glyphs[i].offset = max_offset - atlas->glyphs[i].offset;
	    }

	    TEXTMETRIC metrics = {};
	    GetTextMetrics(device_context, &metrics);
	    atlas->line_spacing = metrics.tmInternalLeading;

	    // sort by area.
	    // for(u32 g = 0; g < GLYPH_COUNT; g++)
	    // {
	    // 	for(s32 h = 0; h < GLYPH_COUNT; h++)
	    // 	{
	    // 	    if((atlas->glyphs[g].width * atlas->glyphs[g].height) < (atlas->glyphs[h].width * atlas->glyphs[h].height))
	    // 	    {
	    // 		// swap.
	    // 		u32*  glyph_ptr = glyphs[g];
	    // 		glyph_header glyph_info = atlas->glyphs[g];

	    // 		CopyMemory(&glyphs[g], &glyphs[h], sizeof(u32*));
	    // 		CopyMemory(&atlas->glyphs[g], &atlas->glyphs[h], sizeof(glyph_header));

	    // 		CopyMemory(&glyphs[h], &glyph_ptr, sizeof(u32*));
	    // 		CopyMemory(&atlas->glyphs[h], &glyph_info, sizeof(glyph_header));
		    
	    // 		h = -1;
	    // 	    }
	    
	    // 	}
	    // }

	    success = true;
	}
	else
	{
	    OutputDebugStringA("'CreateCompatibleDC' failed!\n");
	}
    }
    else
    {
	OutputDebugStringA("'CreateFontA' failed!\n");
    }

    RemoveFontResourceExA(font_file, FR_PRIVATE, 0);

    return(success);
};
internal void
bake_clearglyphs(u32** glyphs)
{
    for(u32 glyph = 0; glyph < GLYPH_COUNT; glyph++)
    {
	VirtualFree(glyphs[glyph], 0, MEM_RELEASE);
    }
}
internal b32
bake_font()
{
    b32 success = false;

    r32 points = (strtof(fontheight_field,0))/2.0f; // total height (ascent + descent)
    s32 pixels = (points/72)*96*(DPI/100);          // total height (ascent + descent)
    
    u32* glyphs[GLYPH_COUNT] = {};
	 
    font_header* atlas = (font_header*)VirtualAlloc(0, sizeof(font_header) + (((pixels*GLYPH_COLUMNS)*(pixels*GLYPH_ROWS))*4), MEM_COMMIT, PAGE_READWRITE);
    atlas->glyph_width  = pixels;
    atlas->glyph_height = pixels;
    atlas->width        = atlas->glyph_width  * GLYPH_COLUMNS;
    atlas->height       = atlas->glyph_height * GLYPH_ROWS;
    atlas->size         = sizeof(font_header) + (atlas->width * atlas->height * 4);
    atlas->glyph_count  = GLYPH_COUNT;
    atlas->glyph_offset = 9 * sizeof(u32);
    atlas->byte_offset  = sizeof(font_header);

    // does the ttf file exist?
    // use windows to query instead of this 
    io_file file = io_readfile(open_file);
    if(file.source)
    {
	io_freefile(file);
    }
    else
    {
	return(success);
    }
    
    if(bake_loadfont(atlas, points, pixels, open_file, glyphs))
    {
	s8* glyph_data = (s8*)atlas + atlas->byte_offset;
	for(s32 g = GLYPH_COUNT - 1; g > -1; g--)
	{
	    u32 target_row    = g / GLYPH_COLUMNS;
	    u32 target_column = g % GLYPH_COLUMNS;

	    // the row height is equal to atlas->glyph_height.
		
	    // bytes contained in a single row =
	    // atlas->glyph_height * atlas->width * 4

	    // bytes contained in a single glyph  row =
	    // atlas->glyph_width * 4

	    // glyph beginning row =
	    // (target_row * 'bytes contained in a single row') + (target_column * 'bytes contained in a single glyph row')

	    // bytes contianed in a single glyph =
	    // atlas->glyph_width * atlas->glyph_height * 4

	    // bytes contianed in entire glyph atlas =
	    // 'bytes contianed in a single glyph' * GLYPH_ROWS * GLYPH_COLUMNS

	    // points to the first row of bytes where the glyph_header should be placed. (bottom-up)
	    s8* target =
	    (glyph_data + (atlas->width * atlas->height * 4) - (atlas->width * atlas->glyph_height * 4))
	    +
	    (atlas->glyph_width * 4 * target_column)
	    -
	    (atlas->width * atlas->glyph_height * 4 * target_row);

	    s8* source = (s8*)(glyphs[g]);
		
	    bake_writeglyph(source, atlas->glyphs[g].width, atlas->glyphs[g].height, atlas->glyphs[g].width, target, atlas->width);

	    // uv.
	    atlas->glyphs[g].u0 = (target_column * atlas->glyph_width)/(r32)atlas->width;
	    atlas->glyphs[g].v0 = ((((GLYPH_ROWS - 1) - target_row) * atlas->glyph_height) + atlas->glyphs[g].height)/(r32)atlas->height;
	    atlas->glyphs[g].u1 = ((target_column * atlas->glyph_width) + atlas->glyphs[g].width)/(r32)atlas->width;
	    atlas->glyphs[g].v1 = (((GLYPH_ROWS - 1) - target_row) * atlas->glyph_height)/(r32)atlas->height;
	}
	
	bake_clearglyphs(glyphs);

	// write font (.font)
	io_writefile(save_file, atlas->size, atlas);
	// write bitmap (.bmp)
	bitmap_saveas(bitmap_file, atlas->width, atlas->height, (s8*)atlas + atlas->byte_offset);

	//VirtualFree(tree.head, 0, MEM_RELEASE);
	VirtualFree(atlas, 0, MEM_RELEASE);

	success = true;
    }
    else
    {
	OutputDebugStringA("'windows_loadfont' failed!\n");
    }
    
    return(success);
}

#define WINDOWS_BUTTON_TRUETYPE 1
#define WINDOWS_BUTTON_SAVE     2
#define WINDOWS_BUTTON_BAKE     3

global HWND window_truetype_field;
global HWND window_save_field;
global HWND window_fontheight_field;

global HWND window_bitmap;
global HWND window_bitmap_label;

internal void windows_userinterface(HWND window)
{
    // structure:
    //
    // pane [0] - settings
    // pane [1] - preview
    //
    // window (1280, 720)
    //
    // pane [0] (0, 0, 640, 720)
    // pane [1] (640, 0, 1280, 720)
    //
    // outer padding is 20px (left, top, right, bottom)
    // inner padding is 10px
    //
    // controls:
    //
    // label  = static
    // field  = edit
    // button = button
    //
    //        20px          30%        5%        65%          20px
    //  [outer padding ] [ label ] [ button ] [ field ] [outer padding]
    //
    
    HFONT font = CreateFontA(30, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    s32 pane_width = window_width/2;
    s32 outer_padding = 20;
    s32 inner_padding = 10;
    s32  line_padding = 50;

    s32  label_width = ((r32)(pane_width - (outer_padding*2) - (inner_padding*2)))*0.30f;
    s32 button_width = ((r32)(pane_width - (outer_padding*2) - (inner_padding*2)))*0.05f;
    s32  field_width = ((r32)(pane_width - (outer_padding*2) - (inner_padding*2)))*0.65f;

    s32 control_height = 40;

    // truetype font.
    HWND window_truetype_label  = CreateWindowA("STATIC", "Truetype font:", WS_CHILD | WS_VISIBLE | SS_CENTER, outer_padding, outer_padding, label_width, control_height, window, 0, 0, 0);
    HWND window_truetype_button = CreateWindowA("BUTTON", "...", WS_CHILD | WS_VISIBLE | WS_BORDER, outer_padding + label_width + inner_padding, outer_padding, button_width, control_height, window, (HMENU)WINDOWS_BUTTON_TRUETYPE, 0, 0);
    window_truetype_field = CreateWindowA("EDIT", "C:/", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, outer_padding + label_width + button_width + (inner_padding*2), outer_padding, field_width, control_height, window, 0, 0, 0);
    
    // save location.
    HWND window_save_label  = CreateWindowA("STATIC", "Save location:", WS_CHILD | WS_VISIBLE | SS_CENTER, outer_padding, outer_padding + line_padding, label_width, control_height, window, 0, 0, 0);
    HWND window_save_button = CreateWindowA("BUTTON", "...", WS_CHILD | WS_VISIBLE | WS_BORDER, outer_padding + label_width + inner_padding, outer_padding + line_padding, button_width, control_height, window, (HMENU)WINDOWS_BUTTON_SAVE, 0, 0);
    window_save_field = CreateWindowA("EDIT", "C:/", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, outer_padding + label_width + button_width + (inner_padding*2), outer_padding + line_padding, field_width, control_height, window, 0, 0, 0);

    // font height.
    HWND window_fontheight_label0 = CreateWindowA("STATIC", "Font height:", WS_CHILD | WS_VISIBLE | SS_CENTER, outer_padding, outer_padding + (line_padding*2), label_width, control_height, window, 0, 0, 0);
    HWND window_fontheight_label1 = CreateWindowA("STATIC", "pt", WS_CHILD | WS_VISIBLE | SS_CENTER, outer_padding + label_width + (field_width*0.1) + (inner_padding*2), outer_padding + (line_padding*2), (label_width*0.15), control_height, window, 0, 0, 0);
    window_fontheight_field  = CreateWindowA("EDIT", "72", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, outer_padding + label_width + inner_padding, outer_padding + (line_padding*2), (field_width*0.1), control_height, window, 0, 0, 0); // no more than three characters! how can we make sure of this?

    // bake.
    HWND window_bake_button = CreateWindowA("BUTTON", "Bake", WS_CHILD | WS_VISIBLE| WS_BORDER, outer_padding, outer_padding + (line_padding*3), pane_width - (outer_padding*2), control_height, window, (HMENU)WINDOWS_BUTTON_BAKE, 0, 0);

    // bitmap.
    window_bitmap = CreateWindowA("STATIC", 0, WS_CHILD | WS_VISIBLE | SS_BITMAP | WS_BORDER, outer_padding + pane_width, outer_padding, 0, 0, window, 0, 0, 0);
    window_bitmap_label = CreateWindowA("STATIC", bitmap_file, WS_CHILD | WS_VISIBLE | SS_CENTER, outer_padding + pane_width, outer_padding + (pane_width - (outer_padding*2)) + inner_padding, (pane_width - (outer_padding*2)), control_height, window, 0, 0, 0);

    // send messages.
    SendMessageA(window_truetype_label,    WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(window_truetype_button,   WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(window_truetype_field,    WM_SETFONT, (WPARAM)font, TRUE); 
    SendMessageA(window_save_label,        WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(window_save_button,       WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(window_save_field,        WM_SETFONT, (WPARAM)font, TRUE); 
    SendMessageA(window_fontheight_label0, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(window_fontheight_label1, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(window_fontheight_field,  WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(window_bake_button,       WM_SETFONT, (WPARAM)font, TRUE);
    //SendMessageA(window_bitmap_label,      WM_SETFONT, (WPARAM)font, TRUE);
}
LRESULT WINAPI windows_procedure_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
    case WM_CLOSE:
    {
	running = false;
    }break;
    case WM_CREATE:
    {
	windows_userinterface(window);
    }break;
    case WM_COMMAND:
    {
	switch(wparam)
	{
	case WINDOWS_BUTTON_TRUETYPE:
	{
	    OPENFILENAME o_file = {};
	    o_file.lStructSize  = sizeof(OPENFILENAME);
	    o_file.hwndOwner    = window;
	    o_file.lpstrFile    = open_file;
	    o_file.lpstrFile[0] = '\0';
	    o_file.nMaxFile     = MAX_PATH;
	    o_file.lpstrFilter  = "Truetype Fonts (.ttf)\0*.TTF\0";
	    o_file.nFilterIndex = 1;

	    if(GetOpenFileNameA(&o_file))
	    {
		SetWindowTextA(window_truetype_field, o_file.lpstrFile);
	    }
	}break;
	case WINDOWS_BUTTON_SAVE:
	{
	    OPENFILENAME s_file = {};
	    s_file.lStructSize  = sizeof(OPENFILENAME);
	    s_file.hwndOwner    = window;
	    s_file.lpstrFile    = save_file;
	    s_file.lpstrFile[0] = '\0';
	    s_file.nMaxFile     = MAX_PATH;
	    s_file.lpstrFilter  = "Font (.font)\0*.FONT\0";
	    s_file.nFilterIndex = 1;

	    if(GetSaveFileNameA(&s_file))
	    {
		SetWindowTextA(window_save_field, s_file.lpstrFile);
	    }
	}break;
	case WINDOWS_BUTTON_BAKE:
	{
	    GetWindowTextA(window_truetype_field, open_file, MAX_PATH);
	    GetWindowTextA(window_save_field, save_file, MAX_PATH);
	    GetWindowTextA(window_fontheight_field, fontheight_field, 3);

	    // bitmap.
	    mem_copy(save_file, bitmap_file, MAX_PATH);
	    s32 len = (s32)strlen(bitmap_file);
	    if(len > 4)
	    {
		bitmap_file[len - 1] = '\0';
		bitmap_file[len - 2] = 'p';
		bitmap_file[len - 3] = 'm';
		bitmap_file[len - 4] = 'b';
	    }
	    
	    if(bake_font())
	    {
		// message box - success!
		MessageBoxA(window, "Success!", "Status", MB_OK);

		s32 pane_width = window_width/2;
		s32 outer_padding = 20;

		// preview!
		HBITMAP bitmap = (HBITMAP)LoadImageA(0, bitmap_file, IMAGE_BITMAP,
						     pane_width - (outer_padding*2),
						     pane_width - (outer_padding*2), LR_LOADFROMFILE);
		if(bitmap)
		{
		    SendMessageA(window_bitmap, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bitmap);
		    SetWindowTextA(window_bitmap_label, bitmap_file);
		}
	    }
	    else
	    {
		// message box - failed!
		MessageBoxA(window, "Failed!", "Status", MB_OK | MB_ICONWARNING);
		
	    }
	}break;
	}
    }break;
    }
    return(DefWindowProcA(window, message, wparam, lparam));
}
    
s32 WINAPI
WinMain (HINSTANCE          instance,
	 HINSTANCE previous_instance,
	 LPSTR     commandline,
	 s32       show_commandline)
{
    // Atlas Baked
    
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    DPI = GetDpiForSystem();

    // Atlas Baked

    if(commandline[0] == '\0')
    {
	WNDCLASSA window_class = {};
	window_class.style 	       = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	window_class.lpfnWndProc   = windows_procedure_message;
	window_class.hInstance     = instance;
	window_class.hCursor       = LoadCursorA(0, IDC_ARROW);
	window_class.hbrBackground = CreateSolidBrush(RGB(112, 169, 161)); // 0x ()
	window_class.lpszClassName = "Atlas Baked";

	if(RegisterClassA(&window_class))
	{
	    RECT window_rect = {
		(GetSystemMetrics(SM_CXSCREEN) - window_width )/2,
		(GetSystemMetrics(SM_CYSCREEN) - window_height)/2,
		window_width  + window_rect.left,
		window_height + window_rect.top,
	    };
	
	    if(!AdjustWindowRect(&window_rect, WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION, false)) {
		OutputDebugStringA("'AdjustWindowRect' failed!\n");
	    }

	    HWND window = CreateWindowA(window_class.lpszClassName,
					window_class.lpszClassName,
					WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION,
					window_rect.left, window_rect.top,
					(window_rect.right - window_rect.left),
					(window_rect.bottom - window_rect.top),
					0, 0,
					instance,
					0);
	
	    if(window)
	    {
		running = true;

		while(running)
		{
		    MSG message = {};
		    while(PeekMessageA(&message, window, 0, 0, PM_REMOVE))
		    {
			TranslateMessage(&message);
			DispatchMessageA(&message);
		    }

		    // here.
		}
	    
		DestroyWindow(window);
	    }
	    else
	    {
		OutputDebugStringA("'CreateWindowA' failed!\n");
	    }
	    UnregisterClassA(window_class.lpszClassName, instance);
	}
	else
	{
	    OutputDebugStringA("'RegisterClassA' failed!\n");
	}
    }
    else
    {
	// Parse
	//
	// Atlas Baked.exe -ttf"a:/truetype/DMMono-Regular.ttf" -s"a:/test/DMMono_72.font" -h"72"
	//
    }

    return(0);
}
