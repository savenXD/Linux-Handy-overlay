"""Browser QA of the shipped Windows shell and native bridge messages.
Run with Python Playwright and Chrome installed; uses a disposable mock, no account.
"""
import os
from pathlib import Path
import shutil
import tempfile
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
PALETTES = [
    dict(light=False,background='#000000',surface='#121212',raised='#222222',text='#ffffff',muted='#a0a0a0',border='#444444'),
    dict(light=True,background='#ffffff',surface='#f3f3f3',raised='#dddddd',text='#000000',muted='#606060',border='#bbbbbb'),
]
with tempfile.TemporaryDirectory(prefix='cronq-ui-') as work, sync_playwright() as p:
    for src in (ROOT/'ui').iterdir():
        if src.is_file(): shutil.copy2(src,Path(work)/src.name)
    shutil.copy2(ROOT/'tests/ui_preview_host.js',Path(work)/'ui_preview_host.js')
    html=Path(work)/'index.html'
    html.write_text(html.read_text().replace('<script src="cronq.js" defer>', '<script src="ui_preview_host.js" defer></script><script src="cronq.js" defer>'))
    browser=p.chromium.launch(executable_path=os.environ.get('CHROME_BIN','/usr/bin/google-chrome'),headless=True,args=['--no-sandbox'])
    page=browser.new_page(viewport=dict(width=360,height=280),device_scale_factor=2)
    errors=[]
    page.on('pageerror',lambda error:errors.append(str(error)))
    page.goto(html.as_uri())
    assert page.locator('.app-button,[data-app="chatgpt"],#appearance,[data-window]').count()==0
    assert page.locator('.bottom-nav button').all_text_contents()==['Previous','Overview','Reload','Report']
    assert page.locator('.bottom-nav button > span:visible,.bottom-nav button > svg:visible').count()==0
    assert page.locator('.bottom-nav button').evaluate_all("buttons=>buttons.every(b=>b.getAttribute('aria-label')&&getComputedStyle(b,'::after').width==='2px'&&b.getBoundingClientRect().width>=40)")
    assert page.locator('.status-bar button').count()==3  # two LEDs + invisible drag surface
    page.locator('#recovery').click()
    assert page.locator('#home').is_visible()
    page.locator('#recovery').dblclick()
    assert page.locator('#chatgpt').is_visible()
    page.locator('#back').click()
    assert page.locator('#home').is_visible()
    page.locator('#recovery').dblclick()
    page.locator('[data-app="home"]').click()
    assert page.locator('#home').is_visible()
    page.locator('#mode').click()
    assert page.locator('#mode').get_attribute('aria-pressed')=='true'
    page.locator('#ghost-pill').click()
    assert page.locator('#ghost-pill').get_attribute('aria-pressed')=='true'
    page.locator('[data-action="reset"]').click()
    assert page.locator('#home').is_visible()
    assert page.locator('#mode').get_attribute('aria-pressed')=='false'
    assert page.locator('#ghost-pill').get_attribute('aria-pressed')=='false'
    page.locator('#snap').click()
    assert page.evaluate("window.__previewEvents.some(m=>m.type==='snap')")
    assert page.locator("#toast,.toast-box").count()==0  # reset/report never create a status bubble
    output=Path(os.environ.get('CRONQ_UI_ARTIFACTS',str(ROOT/'build-mingw/ui-qa')))
    output.mkdir(parents=True,exist_ok=True)
    for mode in range(4):
        palette=PALETTES[mode%2 if mode<2 else (0 if mode==3 else 1)]
        page.evaluate('next=>window.__previewState(next)',dict(appearance=mode,palette=palette))
        for width,height in [(260,180),(360,280),(360,640),(800,600)]:
            page.set_viewport_size(dict(width=width,height=height))
            page.wait_for_timeout(60)
            assert page.locator('#mode').is_visible() and page.locator('#ghost-pill').is_visible()
            assert page.evaluate("""()=>{const p=document.querySelector('#viewport').getBoundingClientRect(),r=document.querySelector('.error-content').getBoundingClientRect();return r.top>=p.top-1&&r.bottom<=p.bottom+1&&r.left>=p.left&&r.right<=p.right;}"""),(mode,width,height)
            if width==360 and height==280:
                page.screenshot(path=str(output/f'windows-error-mode-{mode}.png'))
    # Native content bounds still follow the visible chat slot and a changed viewport.
    page.evaluate('next=>window.__previewState(next)',dict(appearance=0,palette=PALETTES[0]))
    page.locator('#recovery').dblclick()
    page.wait_for_timeout(80)
    assert page.evaluate("window.__previewEvents.some(m=>m.type==='slot'&&m.app==='chatgpt'&&m.w>0&&m.h>0)")
    assert not errors,errors
    browser.close()
    print('ui_error_screen_test: all checks passed')
