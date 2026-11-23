"""Draw the original 16 × 16 Files browser pictograms."""
from pathlib import Path

from PIL import Image, ImageDraw


def eject(draw):
    draw.polygon([(3, 10), (8, 3), (13, 10)], fill="white")
    draw.rectangle((3, 12, 13, 13), fill="white")


def language(draw):
    draw.rounded_rectangle((1, 2, 14, 11), radius=2, outline="white")
    draw.line([(4, 11), (4, 14), (8, 11)], fill="white")
    draw.line([(4, 9), (6, 4), (8, 9)], fill="white")
    draw.line([(5, 7), (7, 7)], fill="white")
    draw.line([(10, 5), (12, 5), (10, 8), (12, 8)], fill="white")


def new_folder(draw):
    draw.polygon([(1, 4), (2, 3), (6, 3), (8, 5), (14, 5),
                  (14, 13), (1, 13)], outline="white")
    draw.line([(5, 9), (10, 9)], fill="white")
    draw.line([(8, 7), (8, 11)], fill="white")


def open_file(draw):
    draw.line([(7, 13), (3, 13), (3, 2), (9, 2), (12, 5), (12, 7)], fill="white")
    draw.line([(9, 2), (9, 5), (12, 5)], fill="white")
    draw.line([(7, 10), (14, 10)], fill="white")
    draw.line([(11, 7), (14, 10), (11, 13)], fill="white")


def usb(draw):
    draw.rounded_rectangle((4, 6, 11, 14), radius=2, outline="white")
    draw.rectangle((5, 2, 10, 6), outline="white")
    draw.point([(7, 4), (8, 4)], fill="white")
    draw.line([(7, 10), (8, 10)], fill="white")


def delete(draw):
    draw.line([(2, 4), (13, 4)], fill="white")
    draw.line([(6, 4), (6, 2), (9, 2), (9, 4)], fill="white")
    draw.line([(4, 6), (5, 13), (10, 13), (11, 6)], fill="white")
    draw.line([(7, 7), (7, 11)], fill="white")
    draw.line([(9, 7), (9, 11)], fill="white")


def main():
    directory = Path(__file__).resolve().parent / "images"
    for pictogram in (eject, language, new_folder, open_file, usb, delete):
        icon = Image.new("RGBA", (16, 16))
        pictogram(ImageDraw.Draw(icon))
        icon.save(directory / f"icon_{pictogram.__name__}.png")


if __name__ == "__main__":
    main()
