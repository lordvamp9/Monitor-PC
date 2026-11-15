"""Export the generated brand emblem to Windows ICO and repository banners.
Requires Pillow. The emblem is original artwork in docs/images/logo.png.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/images'
logo = Image.open(OUT / 'logo.png').convert('RGBA')
logo.save(ROOT / 'icon.ico', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

def make_banner(width, height, name):
    image = Image.new('RGBA', (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            pink = max(0, 1 - ((x-width*.3)**2/(width*.65)**2 + (y-height*.85)**2/(height*1.2)**2))
            pixels[x,y] = (int(8+24*pink), int(9+3*pink), int(30+27*pink), 255)
    draw = ImageDraw.Draw(image)
    for y in range(int(height*.75), height, max(1,int(height*.06))):
        draw.line((0, y, width, y), fill=(99, 35, 119, 100), width=1)
    scale = height/500
    mark = logo.resize((int(410*scale),int(410*scale)), Image.Resampling.LANCZOS)
    image.alpha_composite(mark, (int(35*scale), int(42*scale)))
    font_dir = Path('C:/Windows/Fonts')
    script = ImageFont.truetype(str(font_dir/'segoescb.ttf'), int(104*scale))
    sans = ImageFont.truetype(str(font_dir/'segoeuib.ttf'), int(19*scale))
    small = ImageFont.truetype(str(font_dir/'segoeui.ttf'), int(21*scale))
    text = Image.new('RGBA', image.size)
    td = ImageDraw.Draw(text)
    pos = (int(470*scale), int(108*scale))
    td.text(pos, 'Monitor PC', font=script, fill='#ff56c8', stroke_width=1)
    glow = text.filter(ImageFilter.GaussianBlur(8*scale))
    image.alpha_composite(glow)
    image.alpha_composite(text)
    draw=ImageDraw.Draw(image)
    draw.text((int(493*scale),int(289*scale)), 'REAL TELEMETRY / RETRO SOUL', font=sans, fill='#53eaff')
    draw.text((int(493*scale),int(343*scale)), 'One executable. Open and monitor.', font=small, fill='#e0d9f4')
    draw.text((int(493*scale),int(393*scale)), 'WINDOWS X64  /  CPU + GPU + RAM + FPS', font=sans, fill='#b494ca')
    image.convert('RGB').save(OUT/name, optimize=True)

make_banner(1600,500,'brand-banner.png')
social = Image.new('RGB', (1280,640), '#100c26')
social.paste(Image.open(OUT/'brand-banner.png').resize((1280,400),Image.Resampling.LANCZOS),(0,120))
social.save(OUT/'social-preview.png',optimize=True)
print('Created multi-resolution icon.ico and repository branding banners.')
