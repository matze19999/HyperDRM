"""Build the SDR stimulus used by the measured edge-response wizard."""

import argparse
from pathlib import Path
import subprocess


WIDTH, HEIGHT = 1920, 1080
START, STEP = 4, 2
POSITIONS = (.10, .25, .375, .50, .625, .75, .90)
INNER = ((.27, .30), (.73, .30), (.27, .70), (.73, .70), (.50, .50))
CHECKS = ((0, .44, 'red'), (1, .31, 'blue'), (2, .56, 'white'), (3, .68, 'lime'))


def perimeter_box(edge, along):
    if edge in (0, 2):
        return along, .07 if edge == 0 else .93, .09, .07
    return .93 if edge == 1 else .07, along, .07, .09


def drawbox(step, box, color):
    x, y, half_width, half_height = box
    left, right = round((x - half_width) * WIDTH), round((x + half_width) * WIDTH)
    top, bottom = round((y - half_height) * HEIGHT), round((y + half_height) * HEIGHT)
    start = START + step * STEP
    return (f"drawbox=x={left}:y={top}:w={right-left}:h={bottom-top}:color={color}:t=fill:"
            f"enable='gte(t,{start})*lt(t,{start+STEP})'")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    args = parser.parse_args()
    fields = [(perimeter_box(edge, along), 'white') for edge in range(4) for along in POSITIONS]
    fields += [((x, y, .09, .07), 'white') for x, y in INNER]
    fields += [(perimeter_box(edge, along), color) for edge, along, color in CHECKS]
    fields += [((.45, .52, .09, .07), 'white')]
    filters = ','.join(drawbox(i, box, color) for i, (box, color) in enumerate(fields))
    duration = START + STEP * len(fields) + 2
    video = Path(__file__).resolve().parent / 'app/calibration-response.mp4'
    subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
                    f'color=0x1b1b1b:s={WIDTH}x{HEIGHT}:r=30:d={duration}', '-vf', filters,
                    '-c:v', 'libx264', '-preset', 'fast', '-crf', '16', '-pix_fmt', 'yuv420p',
                    '-profile:v', 'main', '-level', '4.0', '-g', '30', '-an', '-movflags', '+faststart',
                    str(video)], check=True)
    print(video)
