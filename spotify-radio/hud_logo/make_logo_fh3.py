"""The Forza Horizon 3 station logos, 228x104, transparent background. Spotify: its official logo, the icon left
of the wordmark, from spotify_logo_fh3_mask.png (a render of the vector on support.spotify.com). Line-in: white
capitals with an RCA plug for the I of LINE, then IN inverted on a red block. Writes HUD_RadioLogo_Spotify_FH3
and HUD_RadioLogo_LineIn_FH3 (.swatchbin) and a preview on grey into the folder given.
Usage: make_logo_fh3.py [output folder]

FH3's .swatchbin: 0x57-byte header, then a 256x128 BC7 surface, premultiplied alpha, opaque black outside
the logo. Tiled block index bits 0-10 are x0 y0 x1 y1 x2 y2 x3 x4 x5 y3 y4 (from the stock logo)."""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bc7 import encode

S = 8                                   # supersampling
W, H = 228, 104
SURFACE = (256, 128)
BLACK = r"C:\Windows\Fonts\seguibl.ttf"  # Segoe UI Black
WHITE, RED, INK = (248, 248, 248), (232, 44, 50), (12, 12, 14)
SILVER, SILVER_DARK = (214, 218, 224), (120, 126, 136)
# Header of the game's own Groove_Music.swatchbin (Grub/TXCB/TXCH, BC7, 228x104 in 256x128)
HEADER = bytes.fromhex(
    "627572470101000057000000578000000100000042435854020001002c000000570000000080000000800000"
    "484358543002080000000000000000000000000000000001e40068000100e400680001000101002d010048")
assert len(HEADER) == 0x57


def block_xy(i):
    b = lambda n: (i >> n) & 1
    return b(0) | b(2) << 1 | b(4) << 2 | b(6) << 3 | b(7) << 4 | b(8) << 5, b(1) | b(3) << 1 | b(5) << 2 | b(9) << 3 | b(10) << 4


ORDER = [block_xy(i) for i in range(2048)]
assert len(set(ORDER)) == 2048, "layout is not a bijection"


def tile(linear):
    tiled = bytearray(len(linear))
    for i, (x, y) in enumerate(ORDER):
        o = (y * 64 + x) * 16
        tiled[i * 16:i * 16 + 16] = linear[o:o + 16]
    return bytes(tiled)


def untile(tiled):
    linear = bytearray(len(tiled))
    for i, (x, y) in enumerate(ORDER):
        o = (y * 64 + x) * 16
        linear[o:o + 16] = tiled[i * 16:i * 16 + 16]
    return bytes(linear)


def with_halo(c):
    shadow = Image.new("RGBA", c.size, (0, 0, 0, 0))    # the soft edge that keeps it legible on a bright sky
    shadow.putalpha(c.getchannel("A").filter(ImageFilter.GaussianBlur(2.2 * S)).point(lambda v: int(v * 0.55)))
    final = Image.new("RGBA", c.size, (0, 0, 0, 0))
    final.alpha_composite(shadow, (0, S))
    final.alpha_composite(c)
    return final.resize((W, H), Image.LANCZOS)


def spotify_logo():
    mask = Image.open(os.path.join(HERE, "spotify_logo_fh3_mask.png")).convert("L")  # 222 logo pixels wide, x8
    icon_end = 517                                      # the empty column between the icon and the word
    assert mask.crop((icon_end - 2, 0, icon_end + 2, mask.height)).getbbox() is None, "mask layout changed"
    art = Image.new("RGBA", mask.size, (248, 248, 248, 255))
    ImageDraw.Draw(art).rectangle((0, 0, icon_end, art.height), fill=(30, 215, 96, 255))
    art.putalpha(mask)
    c = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    c.alpha_composite(art, ((W * S - art.width) // 2, (H * S - art.height) // 2))
    return with_halo(c)


def text_mask(text, font, spacing=0):
    box = font.getbbox(text)
    pad = 6 * S
    width = box[2] - box[0] if not spacing else int(sum(font.getlength(ch) for ch in text) + spacing * (len(text) - 1))
    m = Image.new("L", (width + 2 * pad, box[3] - box[1] + 2 * pad), 0)
    d = ImageDraw.Draw(m)
    if spacing:
        x = pad
        for ch in text:
            d.text((x, pad - box[1]), ch, font=font, fill=255)
            x += int(font.getlength(ch)) + spacing
    else:
        d.text((pad - box[0], pad - box[1]), text, font=font, fill=255)
    return m.crop(m.getbbox())


def solid(mask, colour):
    im = Image.new("RGBA", mask.size, colour + (255,))
    im.putalpha(mask)
    return im


def line_in_logo():
    c = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    font = ImageFont.truetype(BLACK, 56 * S)
    cap = 46
    while True:                         # the largest capitals that fit
        k = cap * S
        scale = lambda m: m.resize((int(m.width * k / m.height), k), Image.LANCZOS)
        l_mask, ne, in_mask = scale(text_mask("L", font)), scale(text_mask("NE", font)), scale(text_mask("IN", font))
        pad, gap, plug = int(cap * .2 * S), 3 * S, int(cap * .42 * S)
        total = l_mask.width + gap + plug + gap + ne.width + 7 * S + in_mask.width + 2 * pad
        if total <= 220 * S:
            break
        cap -= 1
    x = (W * S - total) // 2
    y = (H * S - k) // 2 - 5 * S
    c.alpha_composite(solid(l_mask, WHITE), (x, y))
    px = x + l_mask.width + gap + plug // 2
    half = plug // 2
    d.rounded_rectangle((px - half * .28, y - 7 * S, px + half * .28, y + k * .24), radius=half * .2, fill=SILVER + (255,))
    d.rectangle((px - half * .78, y + k * .16, px + half * .78, y + k * .4), fill=SILVER + (255,))
    d.line((px, y + k * .16, px, y + k * .4), fill=SILVER_DARK + (255,), width=int(.9 * S))
    d.rounded_rectangle((px - half, y + k * .4, px + half, y + k), radius=half * .3, fill=RED + (255,))
    ne_x = x + l_mask.width + 2 * gap + plug
    c.alpha_composite(solid(ne, WHITE), (ne_x, y))
    bx = ne_x + ne.width + 7 * S
    bw = in_mask.width + 2 * pad
    bh = k + 2 * pad
    d.rounded_rectangle((bx, y - pad, bx + bw, y - pad + bh), radius=int(bh * .14), fill=RED + (255,))
    c.alpha_composite(solid(in_mask, INK), (bx + pad, y))
    tag = text_mask("RADIO", ImageFont.truetype(BLACK, 16 * S), spacing=6 * S)
    fit = min(90 * S / tag.width, 12 * S / tag.height)
    tag = tag.resize((max(1, int(tag.width * fit)), max(1, int(tag.height * fit))), Image.LANCZOS)
    c.alpha_composite(solid(tag, WHITE), (int(bx + bw - 90 * S), int(y + k + pad + 5 * S)))
    return with_halo(c)


def premultiplied(image):
    a = np.asarray(image).astype(np.float64)
    a[..., :3] *= a[..., 3:4] / 255.0
    return a


def write(image, path):
    pm = premultiplied(image)
    surface = np.zeros((SURFACE[1], SURFACE[0], 4))
    surface[..., 3] = 255               # stock files: opaque black outside the logo
    surface[:H, :W] = pm
    surface = np.round(surface).astype(np.uint8)
    data = HEADER + tile(encode(Image.fromarray(surface, "RGBA")))
    back = np.asarray(Image.frombytes("RGBA", SURFACE, untile(data[len(HEADER):]), "bcn", (7,))).astype(int)
    over = lambda x: x[..., :3] + 120 * (1 - x[..., 3:] / 255)
    err = np.abs(over(surface.astype(int)) - over(back))[:H, :W].max()
    if err > 64:
        sys.exit(f"encoding check failed (visible error {err}); nothing written")
    open(path, "wb").write(data)
    print(f"{path}: {len(data)} bytes, visible max error {int(err)}/255")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else HERE
    logos = (("HUD_RadioLogo_Spotify_FH3", spotify_logo()), ("HUD_RadioLogo_LineIn_FH3", line_in_logo()))
    preview = Image.new("RGBA", (W * 3, H * 3 * len(logos)), (70, 74, 82, 255))
    for row, (name, image) in enumerate(logos):
        write(image, os.path.join(out, name + ".swatchbin"))
        over = Image.new("RGBA", (W, H), (70, 74, 82, 255))
        over.alpha_composite(image)
        preview.paste(over.resize((W * 3, H * 3), Image.NEAREST), (0, row * H * 3))
    preview.convert("RGB").save(os.path.join(out, "fh3_logos_preview.png"))
