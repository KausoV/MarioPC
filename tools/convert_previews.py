"""Convert actual native-game PPM framebuffers to readable PNG evidence."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import pygame

def load_ppm(path: Path) -> pygame.Surface:
    with path.open("rb") as file:
        if file.readline() != b"P6\n":
            raise ValueError(f"Expected binary RGB PPM: {path}")
        width, height = map(int, file.readline().split())
        if file.readline() != b"255\n":
            raise ValueError(f"Expected 8-bit RGB: {path}")
        data = file.read()
    if len(data) != width * height * 3:
        raise ValueError(f"Truncated frame: {path}")
    return pygame.image.fromstring(data, (width, height), "RGB")

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("validation", type=Path)
    args = parser.parse_args()
    directory = args.validation.resolve()
    pygame.font.init()
    label = pygame.font.SysFont("Consolas", 17)
    images: dict[str, pygame.Surface] = {}
    results = []
    for ppm in sorted(directory.glob("*.ppm")):
        image = load_ppm(ppm)
        png = ppm.with_suffix(".png")
        pygame.image.save(image, str(png))
        images[ppm.stem] = image
        results.append({"file": png.name, "sha256": hashlib.sha256(png.read_bytes()).hexdigest(),
                        "width": image.get_width(), "height": image.get_height()})
    sheet = pygame.Surface((4*264+8, 8*268+8))
    sheet.fill((22, 24, 30))
    for world in range(1, 9):
        for level in range(1, 5):
            name = f"level-{world}-{level}"
            image = images[name]
            x, y = 8+(level-1)*264, 8+(world-1)*268
            sheet.blit(label.render(f"WORLD {world}-{level}", True, (235,235,235)), (x,y))
            sheet.blit(image, (x,y+20))
    pygame.image.save(sheet, str(directory/"all-32-levels.png"))
    featured = [name for name in ("title","level-1-1","running","jump","fireball","death","respawn","underground","next-level")
                if name in images]
    sample = pygame.Surface((3*520+8, ((len(featured)+2)//3)*508+8))
    sample.fill((22,24,30))
    for index, name in enumerate(featured):
        x,y = 8+(index%3)*520, 8+(index//3)*508
        sample.blit(label.render(name.replace("-"," ").upper(), True, (235,235,235)), (x,y))
        sample.blit(pygame.transform.scale(images[name], (512,480)), (x,y+20))
    pygame.image.save(sample, str(directory/"native-gameplay-preview.png"))
    (directory/"framebuffers-manifest.json").write_text(json.dumps({"source":"actual native C gameplay/raster execution",
                     "files": results}, indent=2)+"\n",encoding="utf-8")
    print(f"Converted {len(results)} actual framebuffers; all-32-levels.png and native-gameplay-preview.png")
    pygame.quit()

if __name__ == "__main__":
    main()

