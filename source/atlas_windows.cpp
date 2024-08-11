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

global s8   open_file[256] = { 'C', ':', '/'};
global s8   save_file[256] = { 'C', ':', '/'};
global s8 bitmap_file[256] = { };

global s8 height_field[256] = { '7', '2' };

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
#define SWAPWORD(x) MAKEWORD(HIBYTE(x), LOBYTE(x))
#define SWAPLONG(x) MAKELONG(SWAPWORD(HIWORD(x)), SWAPWORD(LOWORD(x)))
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
    s8   ascii;
    s32 offset;
    
    s32     spacing;
    s32 pre_spacing;
    
    u32  width;
    u32 height;

    r32 u0;
    r32 u1;
    r32 v0;
    r32 v1;
};
struct font_header
{
    u32   size;
    u32  width;
    u32 height;
    u32  count;

    u32 glyph_height;
    u32 glyph_width;

    u32 line_spacing;

    u32 glyph_offset;
    u32  byte_offset;
    
    glyph_header glyphs[GLYPH_COUNT];
};
#pragma pack(pop)

void font_preview_savebmp(s8* bmp,  u32 bmp_width,  u32 bmp_height,  u8* bmp_data)
{
    u32 bmp_data_size = bmp_width * bmp_height * 4;

    // (A8 R8 G8 B8)
    bitmap_header header = {};
    header.signature      = 0x4D42; 
    header.file_size      = sizeof(bitmap_header) + bmp_data_size;
    header.byte_offset    = sizeof(bitmap_header); 
    header.header_size    = sizeof(BITMAPINFOHEADER); 
    header.width          = bmp_width;  
    header.height         = bmp_height; 
    header.planes         = 1;            
    header.bits_per_pixel = 32;      
    header.compression    = 0;
    header.image_size     = bmp_width * bmp_height * 4;
    // header.      red_mask = 0x00FF0000;
    // header.    green_mask = 0x0000FF00;
    // header.     blue_mask = 0x000000FF;
    // header.    alpha_mask = 0xFF000000;

    u8* save = (u8*)VirtualAlloc(0, header.file_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    CopyMemory((PVOID)save, (VOID*)&header, (SIZE_T)header.header_size);
    CopyMemory((PVOID)(save + header.byte_offset), (VOID*)bmp_data, (SIZE_T)bmp_data_size);

    io_writefile(bmp, header.file_size, save);

    VirtualFree(save, 0, MEM_RELEASE);
}
internal void
write_bitmap(void* source, u32 source_size_x, u32 source_size_y, u32 source_width,
	     void* target, u32 target_width)
{
    for(u32 sy = 0; sy < source_size_y; sy++)
    {
	CopyMemory(target, source, (source_size_x * 4));
	target = (u32*)target + target_width;
	source = (u32*)source + source_width;
    }
}

// cmd. 
internal void
parse_commandline(s8* cmdline, s8* filename, s8* fontname)
{
    u32 c = 0;
    s8 chr = cmdline[c];
    
    while(chr != '\0')
    {
	while(chr != ',')
	{
	    filename[c] = chr;
	    chr = cmdline[++c];
	}
	filename[c + 1] = '\0';
	c += 2;
	chr = cmdline[c];

	u32 counter = 0;
	while(chr != '\0')
	{
	    fontname[counter] = chr;
	    chr = cmdline[++c];
	    counter++;
	}
	fontname[c + 1] = '\0';
	c += 2;
	chr = cmdline[c];
    }
}

// ttf.
b32 ttf_fontfamily(s8* font_file, s8* font_family)
{
    io_file font = io_readfile(font_file);
    if(font.source)
    {
	ttf_offsettable_header* offset_table = (ttf_offsettable_header*)font.source;
	offset_table->num_of_tables = SWAPWORD(offset_table->num_of_tables);

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
		directory_table->length = SWAPLONG(directory_table->length);
		directory_table->offset = SWAPLONG(directory_table->offset);
		break;
	    }
	    directory_table++;
	}

	if(table_found)
	{
	    ttf_nametable_header* name_table = (ttf_nametable_header*)((s8*)font.source + directory_table->offset);

	    name_table->namerecords_count = SWAPWORD(name_table->namerecords_count);
	    name_table->storage_offset    = SWAPWORD(name_table->storage_offset);

	    ttf_name_header* name_header = (ttf_name_header*)((s8*)name_table + sizeof(ttf_nametable_header));
	    for(s32 record = 0; record < name_table->namerecords_count; record++)
	    {
		name_header->name_id = SWAPWORD(name_header->name_id);
		if(name_header->name_id == 1) // font family
		{
		    name_header->string_length = SWAPWORD(name_header->string_length);
		    name_header->string_offset = SWAPWORD(name_header->string_offset);

		    mem_copy((s8*)font.source + directory_table->offset + name_table->storage_offset + 
			     name_header->string_offset + 1,
			     font_family, name_header->string_length - 1);
		    io_freefile(font);
		    return(true);
		}
		name_header++;
	    }
	}
	
    }
    return(false);
}

internal u32*
bake_loadglyph(HFONT font, font_header* atlas, u32 size_px, s8 ascii,
	       u32* glyph_width, u32* glyph_height,
	       s32* offset, s32* spacing, s32* pre_spacing)
{
    HDC device_context = CreateCompatibleDC(GetDC(0));
    
    if(device_context)
    {
	BITMAPINFO bitmap_info              = {};
	bitmap_info.bmiHeader.biSize        =  sizeof(bitmap_info.bmiHeader);
	bitmap_info.bmiHeader.biWidth       =  size_px * 2;
	bitmap_info.bmiHeader.biHeight      =  size_px * 2; // (+) bottom-up, (-) top-down
	bitmap_info.bmiHeader.biPlanes      =  1;
	bitmap_info.bmiHeader.biBitCount    =  32;
	bitmap_info.bmiHeader.biCompression =  BI_RGB;

	void*   bytes = 0;
	HBITMAP bitmap_handle = CreateDIBSection(device_context, &bitmap_info, DIB_RGB_COLORS, &bytes, 0, 0);
	
	u32* bitmap_memory = (u32*)bytes;
	if(bitmap_handle && bitmap_memory)
	{
	    SelectObject (device_context, bitmap_handle);
	    SelectObject (device_context, font);
	    SetBkColor   (device_context, RGB(  0,   0,   0));
	    SetTextColor (device_context, RGB(255, 255, 255));

	    TextOutA(device_context, 0, 0, &ascii, 1);

	    SIZE size;
	    GetTextExtentPoint32A(device_context, &ascii, 1, &size);

	    u32  bb_width  = size_px * 2;
	    u32  bb_height = size_px * 2;
	    u32* bb_memory = (u32*)VirtualAlloc(0, (bb_width * bb_height * 4), MEM_COMMIT, PAGE_READWRITE);

	    // copy and calculate bounds.
	    
	    s32 max_column = 0;
	    s32 min_column = bb_width;
	    s32 max_row    = 0;
	    s32 min_row    = bb_height;

	    u32* ptr = bb_memory;
	    for(u32 by = 0; by < bb_height; by++)  
	    {
		u32* bmp = bitmap_memory;
		for(u32 bx = 0; bx < bb_width; bx++) 
		{
		    u8 a = *bmp++ & 0xff; 
		    if(a)
		    {
			if(bx < min_column) min_column = bx;
			if(bx > max_column) max_column = bx;
			if(by < min_row)    min_row    = by;
			if(by > max_row)    max_row    = by;
		    }
		    *ptr++ = a | (a << 8) | (a << 16) | (a << 24);
		}
		bitmap_memory += (size_px * 2);
	    }
	    
	    *glyph_width  = (max_column != 0) ? ((max_column - min_column) + 1) : 0;
	    *glyph_height = (max_row    != 0) ? ((max_row    - min_row   ) + 1) : 0;

	    *glyph_width  = (*glyph_width  > size_px) ? size_px : *glyph_width;
	    *glyph_height = (*glyph_height > size_px) ? size_px : *glyph_height;

	    // smallest possible glyph.
	    
	    u32* glyph_memory = (u32*)VirtualAlloc(0, *glyph_width * *glyph_height * 4, MEM_COMMIT, PAGE_READWRITE);
	    if(glyph_memory)
	    {
		u32* glyph_ptr = glyph_memory;

		u32* bb_ptr = bb_memory;
		bb_ptr += (min_row * bb_width) + min_column;

		glyph_ptr = glyph_memory;

		write_bitmap(bb_ptr, *glyph_width, *glyph_height, bb_width, glyph_memory, *glyph_width);
	    }

	    // vertical. 
	    TEXTMETRICA metrics = {};
	    GetTextMetricsA(device_context, &metrics);
	    *offset = max_row - (bb_height - metrics.tmAscent);

	    // horizontal.
	    ABC character_metrics = {};
	    GetCharABCWidthsA(device_context, (u32)ascii, (u32)ascii, &character_metrics);
	    *spacing     = character_metrics.abcC;
	    *pre_spacing = character_metrics.abcA;

	    // clean.
	    VirtualFree(bb_memory, 0, MEM_RELEASE);
	    DeleteObject(bitmap_handle);

	    return((u32*)glyph_memory); 
	}
	else
	{
	    OutputDebugStringA("'CreateDIBSection' faliure!\n");
	}
    }
    else
    {
	OutputDebugStringA("'CreateCompatibleDC' faliure!\n");
    }

    return(0);
}
internal b32
bake_loadfont(font_header* atlas, r32 points, s8* font_file, u32** glyphs)
{
    b32 success = false;

    AddFontResourceExA(font_file, FR_PRIVATE, 0);
    s32 font_height = -MulDiv(points, GetDeviceCaps(GetDC(0), LOGPIXELSY), 72);

    s8 font_family[256] = {};
    ttf_fontfamily(font_file, font_family);
    
    HFONT font_handle = CreateFontA(font_height, 0, 0, 0,
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
	s32 character_count = 0;
	s32 max_offset = 0;

	TEXTMETRIC metrics = {};
	GetTextMetrics(GetDC(0), &metrics);

	atlas->line_spacing = metrics.tmInternalLeading;

	for(s32 i = 32; i < 256; i++) // '!'(32) -> 'ÿ'(255) 
	{
	    atlas->glyphs[character_count].ascii = i;
	    glyphs[character_count] = bake_loadglyph(font_handle, atlas, -font_height, i,
						     &atlas->glyphs[character_count].width,
						     &atlas->glyphs[character_count].height,
						     &atlas->glyphs[character_count].offset,
						     &atlas->glyphs[character_count].spacing,
						     &atlas->glyphs[character_count].pre_spacing);
	    
	    if(atlas->glyphs[character_count].offset > max_offset) { max_offset = atlas->glyphs[character_count].offset; }

	    character_count++;
	}

	// sort.

	// 7 3 6 2 9 5 1

	for(u32 g = 0; g < GLYPH_COUNT; g++)
	{
	    for(s32 h = 0; h < GLYPH_COUNT; h++)
	    {
		if((atlas->glyphs[g].width * atlas->glyphs[g].height) < (atlas->glyphs[h].width * atlas->glyphs[h].height))
		{
		    // swap.
		    u32*  glyph_ptr = glyphs[g];
		    glyph_header glyph_info = atlas->glyphs[g];

		    CopyMemory(&glyphs[g], &glyphs[h], sizeof(u32*));
		    CopyMemory(&atlas->glyphs[g], &atlas->glyphs[h], sizeof(glyph_header));

		    CopyMemory(&glyphs[h], &glyph_ptr, sizeof(u32*));
		    CopyMemory(&atlas->glyphs[h], &glyph_info, sizeof(glyph_header));
		    
		    h = -1;
		}
	    
	    }
	}

	for(u32 i = 0; i < 233; i++)
	{
	    atlas->glyphs[i].offset = max_offset - atlas->glyphs[i].offset;
	}

	success = true;
    }
    else
    {
	OutputDebugStringA("'CreateFontA' failed!\n");
	success = false;
    }

    RemoveFontResourceExA(font_file, FR_PRIVATE, 0);

    return(success);
};
internal void
bake_clearglyph_headers(u32** glyph_headers)
{
    for(u32 glyph_header = 0; glyph_header < GLYPH_COUNT; glyph_header++)
    {
	VirtualFree(glyph_headers[glyph_header], 0 , MEM_RELEASE);
    }
}

// packing.
struct packing_rect
{
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;

    u32 width () { return(((x1 - x0) > 0) ? (x1 - x0) : (x0 - x1)); }
    u32 height() { return(((y1 - y0) > 0) ? (y1 - y0) : (y0 - y1)); }
};
struct packing_info
{
    u32 id = 0;
    s32 width  = 0;
    s32 height = 0;
};
struct packing_node
{
    u32 id = 0;
    packing_rect rect = {};
    packing_node* children[2];
};
struct packing_tree
{
    packing_node* head = 0;
    u32 node_count = 1;
};

packing_node* tree_createnode(packing_tree* tree)
{
    return(tree->head + (tree->node_count++));
}
packing_node* tree_insertnode(packing_tree* tree, packing_node* node, packing_info info)
{
    // leaves do not have children.
    
    if(node->children[0])
    {
	// not a leaf.

	packing_node* return_node = tree_insertnode(tree, node->children[0], info);
	if(return_node) return(return_node);
	else            return(tree_insertnode(tree, node->children[1], info));
    }
    else
    {
	// leaf.

	// is there a glyph_header here already?
	if(node->id) return(0); 
	// is it too wide or tall?
	if(info.width > node->rect.width() || info.height > node->rect.height()) return(0);
	// does it fit perfectly?
	if(info.width == node->rect.width() && info.height == node->rect.height())
	{
	    node->id = info.id;
	    return(node);
	}

	// none of the above? split!

	// create children
	node->children[0] = tree_createnode(tree);
	node->children[1] = tree_createnode(tree);

	// do we split length-wise or height-wise?
	s32 dw = node->rect.width() - info.width;
	s32 dh = node->rect.height() - info.height;

	if(dw > dh)
	{
	    // height-wise.
	    node->children[0]->rect = {
		node->rect.x0,              node->rect.y0,
		node->rect.x0 + info.width, node->rect.y1 };
	    node->children[1]->rect = {
		node->rect.x0 + (info.width + 1), node->rect.y0,
		node->rect.x1,                    node->rect.y1 };
	}
	else
	{
	    // length-wise.
	    node->children[0]->rect = {
		node->rect.x0, node->rect.y0,
		node->rect.x1, node->rect.y0 + info.height };
	    node->children[1]->rect = {
		node->rect.x0, node->rect.y0 + (info.height + 1),
		node->rect.x1, node->rect.y1 };
	}

	// insert into first child.
	return (tree_insertnode(tree, node->children[0], info));
    }
}

b32 bake_font()
{
    b32 success = false;
    
    r32 current_pt = 72.0/(DPI/96);
    u32 current_px = 96;
    
    u32* glyphs[GLYPH_COUNT] = {};
	 
    font_header* atlas = (font_header*)VirtualAlloc(0, sizeof(font_header) + ((sqrt(current_px * current_px * GLYPH_COUNT)) * (sqrt(current_px * current_px * GLYPH_COUNT)) * 4), MEM_COMMIT, PAGE_READWRITE);
	
    atlas->glyph_width  = current_px;
    atlas->glyph_height = current_px;
    // atlas->width        = atlas->glyph_header_width  * GLYPH_HEADER_COLUMNS;
    // atlas->height       = atlas->glyph_header_height * GLYPH_HEADER_ROWS;

    atlas->width        = sqrt(atlas->glyph_width * atlas->glyph_height * GLYPH_COUNT);
    atlas->height       = sqrt(atlas->glyph_width * atlas->glyph_height * GLYPH_COUNT);
    atlas->size         = sizeof(font_header) + (atlas->width * atlas->height * 4);
    atlas->count        = GLYPH_COUNT;
    atlas->glyph_offset = 9 * sizeof(u32);
    atlas->byte_offset  = sizeof(font_header);

    io_file file = io_readfile(open_file);
    if(file.source)
    {
	io_freefile(file);
    }
    else
    {
	return(success);
    }
    
    if(bake_loadfont(atlas, current_pt, open_file, glyphs))
    {
	packing_tree tree = {};
	tree.head = (packing_node*)VirtualAlloc(0, sizeof(packing_node) * GLYPH_COUNT * GLYPH_COUNT, MEM_COMMIT, PAGE_READWRITE);
	tree.head->rect = {
	    0, 0, (s32)atlas->width, (s32)atlas->height
	};

	u8* glyph_data = (u8*)atlas + atlas->byte_offset;
	for(s32 g = GLYPH_COUNT - 1; g > -1; g--)
	{
	    u32 target_row    = g / GLYPH_COLUMNS;
	    u32 target_column = g % GLYPH_COLUMNS;

	    // the row height is equal to atlas->glyph_header_height.
		
	    // bytes contained in a single row =
	    // atlas->glyph_header_height * atlas->width * 4

	    // bytes contained in a single glyph_header row =
	    // atlas->glyph_header_width * 4

	    // glyph_header beginning row =
	    // (target_row * 'bytes contained in a single row') + (target_column * 'bytes contained in a single glyph_header row')

	    // bytes contianed in a single glyph_header =
	    // atlas->glyph_header_width * atlas->glyph_header_height * 4

	    // bytes contianed in entire glyph_header atlas =
	    // 'bytes contianed in a single glyph_header' * GLYPH_HEADER_ROWS * GLYPH_HEADER_COLUMNS

	    // points to the first row of bytes where the glyph_header should be placed. (bottom-up)
	    // u8* target =
	    // (glyph_header_data + (atlas->width * atlas->height * 4) - (atlas->width * atlas->glyph_header_height * 4))
	    // +
	    // (atlas->glyph_header_width * 4 * target_column)
	    // -
	    // (atlas->width * atlas->glyph_header_height * 4 * target_row);

	    u8* source = (u8*)(glyphs[g]);
		
	    //write_bitmap(source, atlas->glyph_headers[g].width, atlas->glyph_headers[g].height, atlas->glyph_headers[g].width, target, atlas->width);

	    packing_info info = {};
	    info.id     = g + 1;
	    info.width  = atlas->glyphs[g].width;
	    info.height = atlas->glyphs[g].height;
		
	    packing_node* node = tree_insertnode(&tree, tree.head, info);
	    atlas->glyphs[g].u0 = node->rect.x0/(r32)atlas->width;
	    atlas->glyphs[g].v0 = node->rect.y1/(r32)atlas->height;
	    atlas->glyphs[g].u1 = node->rect.x1/(r32)atlas->width;
	    atlas->glyphs[g].v1 = node->rect.y0/(r32)atlas->height;

	    u8* target = glyph_data + (node->rect.y0 * atlas->width * 4) + (node->rect.x0 * 4);
	    write_bitmap(source, atlas->glyphs[g].width, atlas->glyphs[g].height, atlas->glyphs[g].width, target, atlas->width);
	}
	
	// uv.
	// atlas->glyph_headers[g].u0 = (target_column * atlas->glyph_header_width)/(r32)atlas->width;
	// atlas->glyph_headers[g].v0 = ((((GLYPH_HEADER_ROWS - 1) - target_row) * atlas->glyph_header_height) + atlas->glyph_headers[g].height)/(r32)atlas->height;
	// atlas->glyph_headers[g].u1 = ((target_column * atlas->glyph_header_width) + atlas->glyph_headers[g].width)/(r32)atlas->width;
	// atlas->glyph_headers[g].v1 = (((GLYPH_HEADER_ROWS - 1) - target_row) * atlas->glyph_header_height)/(r32)atlas->height;

	bake_clearglyph_headers(glyphs);

	// write font (.font)
	io_writefile(save_file, atlas->size, atlas);
	// write bitmap (.bmp)
	font_preview_savebmp(bitmap_file, atlas->width, atlas->height, (u8*)atlas + atlas->byte_offset);

	VirtualFree(tree.head, 0, MEM_RELEASE);
	VirtualFree(atlas, 0, MEM_RELEASE);

	success = true;
    }
    else
    {
	OutputDebugStringA("'windows_loadfont' failed!\n");
    }
	
    
    return(success);
}

// baking

#define SELECT_OPEN_BUTTON 1
#define SELECT_SAVE_BUTTON 2
#define BAKE_BUTTON        3

global HWND select_open_window = 0;
global HWND select_save_window = 0;

internal void windows_userinterface(HWND window)
{
    HFONT font = CreateFontA(30, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    
    s32 file_width_static = 174;
    s32 file_width_button = 58;
    s32 file_width_edit   = 928;
    
    HWND static_open_window = CreateWindowA("static", "Truetype font:", WS_CHILD | WS_VISIBLE | SS_CENTER,
					    50, 50,
					    file_width_static, 40, window, 0, 0, 0);
    HWND button_open_window = CreateWindowA("button", "...", WS_CHILD | WS_VISIBLE | WS_BORDER,
					    50 + file_width_static + 10, 50,
					    file_width_button, 40, window, (HMENU)SELECT_OPEN_BUTTON, 0, 0);
    select_open_window = CreateWindowA("edit", "C:/", WS_CHILD | WS_VISIBLE | WS_BORDER,
				       50 + file_width_static + file_width_button + 20, 50,
				       file_width_edit, 40, window, 0, 0, 0);
    SendMessageA(static_open_window, WM_SETFONT, (WPARAM)font, TRUE); 
    SendMessageA(button_open_window, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(select_open_window, WM_SETFONT, (WPARAM)font, TRUE);
    

    HWND static_save_window = CreateWindowA("static", "Save location:", WS_CHILD | WS_VISIBLE | SS_CENTER,
					    50, 100,
					    file_width_static, 40, window, 0, 0, 0);
    HWND button_save_window = CreateWindowA("button", "...", WS_CHILD | WS_VISIBLE | WS_BORDER,
					    50 + file_width_static + 10, 100,
					    file_width_button, 40, window, (HMENU)SELECT_SAVE_BUTTON, 0, 0);
    select_save_window = CreateWindowA("edit", "C:/", WS_CHILD | WS_VISIBLE | WS_BORDER,
				       50 + file_width_static + file_width_button + 20, 100,
				       file_width_edit, 40, window, 0, 0, 0);
    SendMessageA(static_save_window, WM_SETFONT, (WPARAM)font, TRUE); 
    SendMessageA(button_save_window, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(select_save_window, WM_SETFONT, (WPARAM)font, TRUE); 
    
    s32 size_width_static0 = file_width_static;
    s32 size_width_static1 = 20;
    s32 size_width_edit    = 100;
    HWND static0_height_window = CreateWindowA("static", "Font height:", WS_CHILD | WS_VISIBLE | SS_CENTER,
					       50, 150,
					       size_width_static0, 40, window, 0, 0, 0);
    HWND field_height_window = CreateWindowA("edit", height_field, WS_CHILD | WS_VISIBLE | WS_BORDER,
					     50 + size_width_static0 + 10, 150,
					     size_width_edit, 40, window, 0, 0, 0);
    HWND static1_height_window = CreateWindowA("static", "pt", WS_CHILD | WS_VISIBLE,
					       50 + size_width_static0 + size_width_edit + 20, 150,
					       size_width_static1, 40, window, 0, 0, 0);
    SendMessageA(static0_height_window, WM_SETFONT, (WPARAM)font, TRUE); 
    SendMessageA(field_height_window, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageA(static1_height_window, WM_SETFONT, (WPARAM)font, TRUE); 
    
    s32 create_width_button = 393;
    HWND button_create_window = CreateWindowA("button", "Bake", WS_CHILD | WS_VISIBLE| WS_BORDER,
					      50 + create_width_button, 200, create_width_button, 40, window, (HMENU)BAKE_BUTTON, 0, 0);
    SendMessageA(button_create_window, WM_SETFONT, (WPARAM)font, TRUE);

    io_file file0 = io_readfile("a:/test/DMMono_test.bmp");
    io_file file1 = io_readfile("a:/test/DMMono_36.bmp");
    
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
	case SELECT_OPEN_BUTTON:
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
		SetWindowTextA(select_open_window, o_file.lpstrFile);
	    }
	}break;
	case SELECT_SAVE_BUTTON:
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
		SetWindowTextA(select_save_window, s_file.lpstrFile);
	    }
	}break;
	case BAKE_BUTTON:
	{
	    GetWindowTextA(select_open_window, open_file, MAX_PATH);
	    GetWindowTextA(select_save_window, save_file, MAX_PATH);
	    // GetWindowTextA(); - font height

	    // bitmap.
	    mem_copy(save_file, bitmap_file, MAX_PATH);
	    s32 len = strlen(bitmap_file);
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
		MessageBoxA(window, "success!", "Status", MB_OK);

		// preview!
		HWND bitmap_window = CreateWindowA("static", 0, WS_CHILD | WS_VISIBLE | SS_BITMAP | WS_BORDER,
						   450, 290,
						   380, 380, window, 0, 0, 0);
		HBITMAP bitmap = (HBITMAP)LoadImageA(0, bitmap_file, IMAGE_BITMAP, 380, 380, LR_LOADFROMFILE | LR_MONOCHROME);
		SendMessageA(bitmap_window, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bitmap);
	    }
	    else
	    {
		// message box - failed!
		MessageBoxA(window, "failed!", "Status", MB_OK | MB_ICONWARNING);
		
	    }
	}break;
	}
    }break;
    }
    return(DefWindowProcA(window, message, wparam, lparam));
}
    
s32 WINAPI
WinMain (HINSTANCE instance,
	 HINSTANCE prev_instance,
	 LPSTR     lpCmdLine,
	 s32       nShowCmd)
{
    // Atlas Baked
    
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    DPI = GetDpiForSystem();

    // Atlas Baked

    WNDCLASSA window_class = {};
    window_class.style 	       = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc   = windows_procedure_message;
    window_class.hInstance     = instance;
    window_class.hCursor       = LoadCursorA(0, IDC_ARROW);
    window_class.hbrBackground = CreateSolidBrush(RGB(112, 169, 161));
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

    return(0);
}
