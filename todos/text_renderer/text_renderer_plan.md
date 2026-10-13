# World-Space Icon Rendering — Follow-up: `submit_text`

Vector icon rendering (`Renderer3D::submit_icon`, heap-mode `SlugIcon` port) is done and in use.

Remaining work: `submit_text`, reusing the same pass/queue/SSBO-region design (the font region
already reserved past the icon region, `vk_context.h:193-201`) with additional per-glyph instancing
on top of the same instanced-draw mechanism. Not yet planned in detail — do that when picked up.