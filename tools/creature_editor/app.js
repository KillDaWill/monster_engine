import {Viewport} from './viewport.js';
import {connectAgent} from './agent-sync.js';

const $=id=>document.getElementById(id),clone=x=>structuredClone(x);
const stages=['seed','larva','juvenile','adult'];
const stageNames=['Semilla','Larva','Juvenil','Adulto'];
const groups={body:['Cuerpo','La silueta empieza aquí.','◒'],head:['Cabeza','Un rostro con carácter propio.','◁'],limbs:['Extremidades','Apoyos, proporciones y movimiento.','⋔'],tails:['Cola','Equilibrio y una silueta singular.','∿'],ornaments:['Ornamentos','Cuernos, espinas y crestas.','♧']};
const labels={totalScale:'Escala general',trunkLength:'Longitud del tronco',thoraxWidth:'Anchura del tórax',thoraxHeight:'Altura del tórax',abdomenWidth:'Anchura del abdomen',abdomenHeight:'Altura del abdomen',pelvicWidth:'Anchura de la pelvis',pelvicHeight:'Altura de la pelvis',neckLength:'Longitud del cuello',neckWidth:'Grosor del cuello',withersHeight:'Altura de la cruz',bodyFlattening:'Aplanamiento',shoulderWidth:'Anchura de hombros',length:'Longitud',thickness:'Grosor',development:'Desarrollo',sprawl:'Apertura lateral',footScale:'Tamaño del pie',digitSpread:'Separación de dedos',digitCount:'Número de dedos',curvature:'Curvatura',baseWidth:'Anchura de la base',baseHeight:'Altura de la base',tipWidth:'Anchura de la punta',tipHeight:'Altura de la punta',taperCurve:'Afinamiento',segmentCount:'Segmentos',skullWidth:'Anchura del cráneo',skullHeight:'Altura del cráneo',skullLength:'Longitud del cráneo',muzzleLength:'Longitud del hocico',muzzleWidth:'Anchura del hocico',muzzleTaper:'Afinamiento del hocico',earSize:'Tamaño de las orejas',earPointiness:'Punta de las orejas',jawStrength:'Fuerza mandibular',eyeSize:'Tamaño de los ojos',cheekMass:'Volumen de mejillas',baseRadius:'Radio de la base',tipRadius:'Radio de la punta',lateralSweep:'Arco lateral',pathU:'Posición sobre el cuerpo',lateralOffset:'Desplazamiento lateral',scale:'Escala',widthScale:'Anchura',heightScale:'Altura',lengthScale:'Longitud',patternStrength:'Intensidad del patrón',patternScale:'Escala del patrón',coverage:'Cobertura',size:'Tamaño',relief:'Relieve',roughness:'Rugosidad',density:'Densidad',strength:'Intensidad',sharpness:'Nitidez',proximalSweep:'Inclinación proximal',distalSweep:'Inclinación distal',footYaw:'Rotación del pie',flexionScale:'Flexión articular'};
const label=key=>labels[key]||key.replace(/([A-Z])/g,' $1').replace(/^./,c=>c.toUpperCase());
let doc=null,source=null,entries=[],stage='adult',part='body',tab='shape',index=0,dirty=false,loadSerial=0,limbDefaults={};
let undo=[],redo=[],revision=0,timer=null,busy=false,pending=false,fitNext=true,viewport=null,handles=[];
let cameraSelection='perspective';
function toast(message){$('toast').textContent=message;$('toast').hidden=false;clearTimeout(toast.timer);toast.timer=setTimeout(()=>$('toast').hidden=true,3500);}
function showError(message){$('error').textContent=message;$('error').hidden=!message;}
async function api(path,body,binary=false){const r=await fetch(path,body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});if(!r.ok){let message;try{message=(await r.json()).error;}catch{message=`Error ${r.status}`;}throw Error(message);}return binary?r.arrayBuffer():r.json();}
function updateDirty(){ $('dirty').textContent=dirty?'Cambios sin guardar':source?.startsWith('builtin:')?'Receta del motor · guardar crea una copia':'Guardado en archivos locales';$('undo').disabled=!undo.length;$('redo').disabled=!redo.length;}
function checkpoint(){if(!doc)return;const value=JSON.stringify(doc);if(undo.at(-1)!==value){undo.push(value);if(undo.length>50)undo.shift();}redo=[];updateDirty();}
function changed(){dirty=true;revision++;updateDirty();$('title').textContent=doc.recipe.name||'Una nueva especie';clearTimeout(timer);timer=setTimeout(preview,320);}
function mutate(fn,rebuild=true){const before=clone(doc);checkpoint();try{fn();changed();if(rebuild)render();}catch(e){doc=before;render();showError(e.message);}}
function history(from,to){if(!from.length)return;to.push(JSON.stringify(doc));doc=JSON.parse(from.pop());index=0;changed();render();}
function p(){return doc.recipe[stage];}
function selected(){return part==='body'?p().axial:part==='head'?p().head:p()[part][index];}
function matchingModule(){const kind={body:0,head:1,limbs:2,tails:3,ornaments:4}[part];return doc.recipe.bodyPlan.modules.find(m=>m.kind===kind&&((kind<2)||m.phenotypeIndex===index));}
function siblings(object){
    const own=p().limbs.indexOf(object);
    if(part!=='limbs'||own<0||!$('symmetry').checked)return[object];
    const modules=doc.recipe.bodyPlan.modules,host=modules.find(m=>m.kind===2&&m.phenotypeIndex===own);
    if(!host)return[object];
    return p().limbs.filter((l,i)=>{if(i===own)return true;if(l.role!==object.role||l.side===object.side)return false;
        const m=modules.find(m=>m.kind===2&&m.phenotypeIndex===i);
        if(!m||m.attachment.hostModuleInstanceId!==host.attachment.hostModuleInstanceId||m.pathAttached!==host.pathAttached)return false;
        if(m.pathAttached)return m.pathNodeA===host.pathNodeA&&m.pathNodeB===host.pathNodeB&&Math.abs(m.pathU-host.pathU)<.001;
        return Math.floor((m.attachment.slotId-2)/2)===Math.floor((host.attachment.slotId-2)/2);
    });
}
function setField(object,key,value){
    if(part==='limbs'&&doc.recipe.bodyPlan.modules.includes(object)){
        const peers=siblings(p().limbs[object.phenotypeIndex]);
        for(const limb of peers){const module=doc.recipe.bodyPlan.modules.find(m=>m.kind===2&&m.phenotypeIndex===p().limbs.indexOf(limb));
            if(module)module[key]=key==='lateralOffset'&&module!==object?-value:value;}
    }else for(const o of siblings(object)){o[key]=value;if(key==='archetype'&&value===6&&p().limbs.includes(o))o.digitCount=2;}
    if(object===p().head&&key==='eyeSize')p().eyes.size=value;
}
async function refreshLibrary(){entries=await api('/api/library');renderLibrary();}
function renderLibrary(){const list=$('library');list.replaceChildren();$('library-count').textContent=entries.length;
    entries.forEach((e,i)=>{const b=document.createElement('button');b.className='library-card'+(source===e.key?' active':'');const icon=document.createElement('span');icon.className='creature-icon';icon.textContent=['∿','♧','♉','◁'][i%4];const text=document.createElement('div'),strong=document.createElement('strong'),small=document.createElement('small');strong.textContent=e.name;small.textContent=e.builtin?'Receta del motor':'Receta local';text.append(strong,small);b.append(icon,text);b.onclick=()=>load(e.key);list.append(b);});}
async function load(key,asNew=false){if(dirty&&!confirm('Hay cambios sin guardar. ¿Descartarlos y abrir otra criatura?'))return;
    const serial=++loadSerial;
    try{const next=await api('/api/recipe/'+encodeURIComponent(key));if(serial!==loadSerial)return;doc=next;
        for(const key of stages)if(doc.recipe[key].limbs.length)limbDefaults[key]=clone(doc.recipe[key].limbs);
        source=asNew?null:key;if(asNew){doc.recipe.id=nextId();doc.recipe.name='Nueva criatura';}dirty=asNew;undo=[];redo=[];index=0;part='body';fitNext=true;revision++;render();renderLibrary();preview();}catch(e){showError(e.message);}}
function nextId(){return Math.max(999,...entries.map(e=>Number(e.id)||0))+1;}
function render(){if(!doc)return;let obj=selected();if(!obj&&['limbs','tails','ornaments'].includes(part)){index=Math.max(0,p()[part].length-1);obj=selected();}
    $('name').value=doc.recipe.name;$('title').textContent=doc.recipe.name;$('part-title').textContent=groups[part][0];$('part-description').textContent=groups[part][1];
    document.querySelectorAll('[data-stage]').forEach(b=>b.classList.toggle('active',b.dataset.stage===stage));document.querySelectorAll('[data-tab]').forEach(b=>b.classList.toggle('active',b.dataset.tab===tab));
    renderParts();renderControls();buildHandles();updateDirty();}
function renderParts(){const container=$('parts');container.replaceChildren();for(const [key,[name,,icon]] of Object.entries(groups)){const b=document.createElement('button');b.className='part-card'+(part===key?' active':'');const s=document.createElement('span'),strong=document.createElement('strong'),small=document.createElement('small');s.textContent=icon;strong.textContent=name;small.textContent=key==='body'?'Estructura axial':key==='head'?'Cráneo y rostro':`${p()[key].length} piezas`;b.append(s,strong,small);b.onclick=()=>{part=key;index=0;tab='shape';render();};container.append(b);}}
function section(title){const d=document.createElement('div');d.className='control-section';d.textContent=title;$('controls').append(d);}
function number(object,key,{min,max,step=.01,integer=false,note}={}){const value=object[key];if(typeof value!=='number')return;
    const unit=/development|coverage|Strength|sharpness|sprawl|earSize|eyeSize|muzzleTaper|skullWidth|skullHeight|skullLength|jawStrength|cheekMass/.test(key);
    min=min??(/curvature|Sweep|Offset|Yaw/.test(key)?-3:0);max=max??(unit?Math.max(1,value):Math.max(2,value*2.5));
    if(key==='totalScale'){min=.2;max=4;}if(key==='digitCount'){min=0;max=8;integer=true;}if(key==='segmentCount'){min=2;max=16;integer=true;}if(integer)step=1;
    const d=document.createElement('div');d.className='control';const l=document.createElement('label');l.textContent=label(key);const n=document.createElement('input');n.type='number';n.value=Number(value.toFixed(4));n.min=min;n.max=max;n.step=step;n.setAttribute('aria-label',label(key));const slider=document.createElement('input');slider.type='range';slider.min=min;slider.max=max;slider.step=step;slider.value=value;slider.setAttribute('aria-label',label(key));l.append(n);d.append(l,slider);
    const apply=(v)=>{if(!Number.isFinite(v))return;v=Math.max(min,Math.min(max,v));if(integer)v=Math.round(v);setField(object,key,v);n.value=Number(v.toFixed(4));slider.value=v;changed();};
    slider.onpointerdown=checkpoint;slider.onkeydown=e=>{if(e.key.startsWith('Arrow'))checkpoint();};slider.oninput=()=>apply(Number(slider.value));n.onfocus=checkpoint;n.onchange=()=>apply(Number(n.value));
    if(note){const small=document.createElement('small');small.textContent=note;d.append(small);}$('controls').append(d);
}
function select(object,key,options,title){const d=document.createElement('div');d.className='control';const l=document.createElement('label');l.textContent=title||label(key);const s=document.createElement('select');s.setAttribute('aria-label',l.textContent);for(const [value,text]of options){const o=new Option(text,value);s.add(o);}s.value=object[key];s.onchange=()=>mutate(()=>setField(object,key,Number(s.value)));d.append(l,s);$('controls').append(d);}
function color(object,key,title){const d=document.createElement('div');d.className='control';const l=document.createElement('label');l.textContent=title;const input=document.createElement('input');input.type='color';input.value='#'+['r','g','b'].map(c=>object[key][c].toString(16).padStart(2,'0')).join('');input.setAttribute('aria-label',title);input.onfocus=checkpoint;input.oninput=()=>{const v=input.value.slice(1);for(const [i,c] of ['r','g','b'].entries())object[key][c]=parseInt(v.slice(i*2,i*2+2),16);changed();};d.append(l,input);$('controls').append(d);}
function piecePicker(){if(!['limbs','tails','ornaments'].includes(part))return;const row=document.createElement('div');row.className='piece-row';const s=document.createElement('select');s.setAttribute('aria-label','Pieza seleccionada');p()[part].forEach((o,i)=>{let name=`${groups[part][0]} ${i+1}`;if(part==='limbs')name=`${o.role===0?'Delantera':'Trasera'} ${o.side===0?'izquierda':'derecha'} · ${i+1}`;s.add(new Option(name,i));});s.value=index;s.onchange=()=>{index=Number(s.value);render();};const add=document.createElement('button');add.textContent='＋';add.title=part==='limbs'?'Añadir par de extremidades':'Añadir pieza';add.onclick=addPart;const remove=document.createElement('button');remove.textContent='−';remove.title='Eliminar pieza';remove.disabled=!p()[part].length;remove.onclick=removePart;row.append(s,add,remove);$('controls').append(row);}
function renderControls(){const container=$('controls');container.replaceChildren();piecePicker();const o=selected();if(tab==='paint'){paint();return;}if(tab==='advanced'){advanced();return;}if(!o){const text=document.createElement('p');text.textContent='Añade una pieza con ＋ para empezar.';container.append(text);return;}
    if(part==='body'){section('Proporciones');['totalScale','trunkLength','thoraxWidth','thoraxHeight','abdomenWidth','pelvicWidth','neckLength','neckWidth'].forEach(k=>number(o,k));if(o.archetype===1)number(o,'withersHeight');}
    if(part==='head'){select(o,'archetype',[[0,'Reptil'],[1,'Mamífero'],[2,'Ave']],'Familia de cabeza');section('Cráneo y rostro');['skullWidth','skullHeight','skullLength','muzzleLength','muzzleWidth','muzzleTaper','jawStrength','cheekMass','eyeSize','earSize','earPointiness'].forEach(k=>number(o,k));section('Envolvente');for(const k of ['scale','widthScale','heightScale','lengthScale'])number(p().headEnvelope,k);}
    if(part==='limbs'){select(o,'archetype',[[0,'Reptil'],[1,'Mamífero'],[6,'Ungulado']],'Anatomía del miembro');['length','thickness','sprawl','footScale','digitSpread','digitCount','development'].forEach(k=>number(o,k));}
    if(part==='tails'){select(o,'archetype',[[0,'Ahusada'],[1,'Látigo'],[2,'Robusta'],[3,'Prensil'],[4,'Aleta']],'Forma de cola');['length','baseWidth','baseHeight','tipWidth','curvature','taperCurve','segmentCount','development'].forEach(k=>number(o,k));}
    if(part==='ornaments'){select(o,'archetype',[[0,'Cuerno'],[1,'Espina'],[2,'Membrana'],[3,'Placa'],[4,'Cresta']],'Tipo de ornamento');['length','baseRadius','tipRadius','curvature','lateralSweep','development'].forEach(k=>number(o,k));}
    const m=matchingModule();if(m?.pathAttached){section('Anclaje');number(m,'pathU',{min:0,max:1});number(m,'lateralOffset',{min:-2,max:2});}
}
function paint(){const s=p().surface;section('Paleta de la criatura');color(s.pigment,'baseColor','Color principal');color(s.pigment,'secondaryColor','Color secundario');color(s.pigment,'ventralColor','Vientre');color(p().eyes,'irisColor','Iris');section('Pigmentación');number(s.pigment,'patternStrength',{min:0,max:1});number(s.pigment,'patternScale',{min:.1,max:10});
    if(s.pigment.layerCount){const layer=s.pigment.layers[0];select(layer,'pattern',[[0,'Liso'],[1,'Ruido'],[2,'Motas'],[3,'Bandas'],[4,'Rayas'],[5,'Manchas'],[6,'Ocelos'],[7,'Degradado']],'Patrón de la primera capa');color(layer,'color','Color del patrón');number(layer,'strength',{min:0,max:1});number(layer,'scale',{min:.1,max:15});}
    else{const b=document.createElement('button');b.textContent='＋ Añadir patrón';b.onclick=()=>mutate(()=>{s.pigment.layers[0]={pattern:2,color:{r:48,g:65,b:37,a:255},strength:.7,scale:2,sharpness:.5,direction:{x:0,y:0,z:1},seed:42};s.pigment.layerCount=1;});$('controls').append(b);}
    section('Cobertura');select(s.integument,'type',[[0,'Piel lisa'],[1,'Escamas'],[2,'Pelaje'],[3,'Plumas'],[4,'Placas']],'Tegumento');number(s.integument,'coverage',{min:0,max:1});const note=document.createElement('p');note.textContent='La vista muestra geometría y pigmento. El pelo y el relieve del shader se conservan en la receta y se ven en el visor del motor.';note.style.cssText='font-size:10px;color:#819080;line-height:1.7';$('controls').append(note);
}
function advanced(){section('Datos completos de la parte');const o=selected();if(o)walk(o,$('controls'));
    section('Documento completo');const text=document.createElement('textarea');text.setAttribute('aria-label','JSON de la receta');text.value=JSON.stringify(doc,null,2);const b=document.createElement('button');b.textContent='Validar y aplicar JSON';b.className='wide';b.onclick=async()=>{b.disabled=true;try{const validated=await api('/api/validate',{document:JSON.parse(text.value)});mutate(()=>{doc=validated;index=0;});toast('Documento validado por el motor.');}catch(e){showError(e.message);}finally{b.disabled=false;}};$('controls').append(text,b);
}
function walk(object,parent,depth=0){for(const [key,value]of Object.entries(object)){
    if(typeof value==='number'){const prior=$('controls');number(object,key,{integer:/Count|seed|archetype|Form|Layout|Shape|role|side|Phalanges/.test(key),max:/seed/.test(key)?4294967295:undefined});const field=prior.lastElementChild;if(parent!==prior)parent.append(field);}
    else if(value&&typeof value==='object'&&depth<3){const details=document.createElement('details'),summary=document.createElement('summary');summary.textContent=label(key);details.append(summary);parent.append(details);walk(value,details,depth+1);}
}}
function freeModuleId(){const ids=new Set(doc.recipe.bodyPlan.modules.map(m=>m.instanceId));for(let i=1;i<65535;i++)if(!ids.has(i))return i;throw Error('No quedan identificadores de módulo.');}
function moduleTemplate(kind,phenotypeIndex){const axial=doc.recipe.bodyPlan.modules.find(m=>m.kind===0).instanceId;return{instanceId:freeModuleId(),kind,attachment:{hostModuleInstanceId:axial,slotId:kind===3?6:7},phenotypeIndex,pathAttached:kind!==3,pathNodeA:axial*65536+6,pathNodeB:axial*65536+2,pathU:.5,lateralOffset:0,pathNormal:{x:0,y:1,z:0},membranePrevious:0};}
function addPart(){const key=part,limit={limbs:16,tails:4,ornaments:64}[key],kind={limbs:2,tails:3,ornaments:4}[key],amount=key==='limbs'?2:1;if(p()[key].length+amount>limit||doc.recipe.bodyPlan.moduleCount+amount>64){toast('Se alcanzó el límite de piezas del motor.');return;}
    mutate(()=>{index=p()[key].length;for(let k=0;k<amount;k++){const idx=index+k,m=moduleTemplate(kind,idx);if(key==='limbs'){m.attachment.slotId=k?3:2;m.pathNormal={x:k?-1:1,y:0,z:0};m.pathU=.25+(Math.floor(idx/2)%3)*.17;}
        if(key==='ornaments')m.pathU=.2+(idx%5)*.15;
        doc.recipe.bodyPlan.modules.push(m);for(const stageKey of stages){const phenotype=doc.recipe[stageKey];let value;
            if(key==='ornaments')value={archetype:0,length:1,baseRadius:.17,tipRadius:.025,curvature:.1,development:phenotype.development.appendages,growthDirection:{x:0,y:1,z:0},lateralSweep:0};
            else if(key==='tails'){value=clone(phenotype.tails[0]||{archetype:0,length:2,baseWidth:.2,baseHeight:.2,tipWidth:.03,tipHeight:.03,taperCurve:1.2,curvature:0,development:1,rootMatchStrength:0,segmentCount:5});if(idx)value.curvature=(idx%2?1:-1)*(1+idx*.5);}
            else{const ref=phenotype.limbs.find(l=>l.side===k)||phenotype.limbs[0]||limbDefaults[stageKey]?.[k]||limbDefaults[stageKey]?.[0];if(!ref)throw Error('Importa una receta con extremidades para usarla como plantilla.');value=clone(ref);value.side=k;}
            phenotype[key].push(value);phenotype[{limbs:'limbCount',tails:'tailCount',ornaments:'ornamentCount'}[key]]=phenotype[key].length;}}
        doc.recipe.bodyPlan.moduleCount=doc.recipe.bodyPlan.modules.length;});
}
function removePart(){const o=selected();if(!o)return;const kind={limbs:2,tails:3,ornaments:4}[part];const removed=part==='limbs'?siblings(o).map(l=>p().limbs.indexOf(l)):[index];
    mutate(()=>{const modules=doc.recipe.bodyPlan.modules,ids=new Set(modules.filter(m=>m.kind===kind&&removed.includes(m.phenotypeIndex)).map(m=>m.instanceId));
        // No dejar referencias colgantes al borrar una estación de membrana.
        if(modules.some(m=>!ids.has(m.instanceId)&&(ids.has(m.attachment.hostModuleInstanceId)||ids.has(m.pathNodeA>>>16)||ids.has(m.pathNodeB>>>16)||ids.has(m.membranePrevious)))){toast('Elimina antes las piezas ancladas a esta parte.');return;}
        doc.recipe.bodyPlan.modules=modules.filter(m=>!ids.has(m.instanceId));for(const m of doc.recipe.bodyPlan.modules)if(m.kind===kind)m.phenotypeIndex-=removed.filter(i=>i<m.phenotypeIndex).length;
        for(const stageKey of stages){const phenotype=doc.recipe[stageKey];phenotype[part]=phenotype[part].filter((_,i)=>!removed.includes(i));phenotype[{limbs:'limbCount',tails:'tailCount',ornaments:'ornamentCount'}[part]]=phenotype[part].length;}
        doc.recipe.bodyPlan.moduleCount=doc.recipe.bodyPlan.modules.length;index=0;});
}
function buildHandles(){const layer=$('handles');layer.replaceChildren();handles=[];if(!viewport||!doc)return;for(const m of doc.recipe.bodyPlan.modules){const nodes=viewport.nodes.filter(n=>n.module===m.instanceId);if(!nodes.length)continue;const point=nodes[Math.floor(nodes.length/2)].position;const button=document.createElement('button');button.className='handle'+(matchingModule()?.instanceId===m.instanceId?' active':'');button.title=groups[['body','head','limbs','tails','ornaments'][m.kind]][0]+' · Seleccionar';button.setAttribute('aria-label',button.title);button.onclick=()=>{part=['body','head','limbs','tails','ornaments'][m.kind];index=m.phenotypeIndex;tab='shape';render();};layer.append(button);handles.push({point,button});}positionHandles();}
function positionHandles(){if(!viewport)return;for(const h of handles){const xy=viewport.project(h.point);h.button.hidden=!xy||!$('show-handles').checked;if(xy){h.button.style.left=xy[0]+'px';h.button.style.top=xy[1]+'px';}}}
async function preview(){if(!doc||!viewport)return;if(busy){pending=true;return;}busy=true;pending=false;const rev=revision,documentSnapshot=clone(doc),requestedStage=stage;const started=performance.now();$('preview-status').textContent='Construyendo anatomía…';
    try{const data=await api('/api/preview',{document:documentSnapshot,stage:stages.indexOf(requestedStage),quality:$('quality').value},true);if(revision!==rev||stage!==requestedStage){pending=true;return;}const stats=viewport.load(data,fitNext);fitNext=false;buildHandles();showError('');$('preview-status').textContent=`${stageNames[stages.indexOf(stage)]} · Anatomía del motor`;$('mesh-stats').textContent=`${stats.triangles.toLocaleString('es')} triángulos · ${((performance.now()-started)/1000).toFixed(1)} s de reconstrucción`;}
    catch(e){if(revision===rev){showError(e.message);$('preview-status').textContent='Revisa los cambios de la receta';}}
    finally{busy=false;if(pending){pending=false;clearTimeout(timer);timer=setTimeout(preview,50);}}
}
const slug=s=>s.normalize('NFD').replace(/[\u0300-\u036f]/g,'').toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'').slice(0,80)||'criatura';
function openSave(){if(!doc)return;const local=source&&!source.startsWith('builtin:');$('filename').value=local?source.replace('.creature.json',''):slug(doc.recipe.name);$('recipe-id').min=local&&doc.recipe.id<1000?1:1000;$('recipe-id').value=local?doc.recipe.id:nextId();$('save-description').textContent=local?'Actualiza tu archivo de receta.':'Se creará una receta externa con identidad propia.';$('save-error').textContent='';$('save-dialog').showModal();}
$('save-form').onsubmit=async e=>{e.preventDefault();const button=e.submitter;button.disabled=true;try{const snapshot=clone(doc);snapshot.recipe.id=Number($('recipe-id').value);const filename=$('filename').value+'.creature.json';const result=await api('/api/save',{document:snapshot,filename,overwrite:source===filename,catalog_override:source===filename&&snapshot.recipe.id<1000});checkpoint();doc=result.document;source=result.key;dirty=false;revision++;$('save-dialog').close();render();await refreshLibrary();toast('Criatura guardada en recipes/'+result.key);}catch(error){$('save-error').textContent=error.message;}finally{button.disabled=false;}};
$('name').onfocus=checkpoint;$('name').oninput=()=>{if(!doc)return;doc.recipe.name=$('name').value;changed();};
$('undo').onclick=()=>history(undo,redo);$('redo').onclick=()=>history(redo,undo);
document.querySelectorAll('[data-stage]').forEach(b=>b.onclick=()=>{if(!doc)return;stage=b.dataset.stage;index=0;revision++;fitNext=true;render();preview();});
document.querySelectorAll('[data-tab]').forEach(b=>b.onclick=()=>{tab=b.dataset.tab;render();});
document.querySelectorAll('[data-view]').forEach(b=>b.onclick=()=>{cameraSelection=b.dataset.view;viewport?.view(cameraSelection);});
$('quality').onchange=()=>{revision++;preview();};$('fit').onclick=()=>viewport?.fit();$('turntable').onclick=()=>{if(!viewport)return;viewport.turn=!viewport.turn;$('turntable').textContent=viewport.turn?'Ⅱ Pausa':'↻ Giro';};
$('save').onclick=openSave;$('cancel-save').onclick=()=>$('save-dialog').close();$('show-handles').onchange=positionHandles;
$('copy-stage').onclick=()=>{if(!doc||!confirm('¿Reemplazar la forma de las otras tres etapas por la etapa actual?'))return;mutate(()=>{const copy=clone(p());for(const key of stages)if(key!==stage)doc.recipe[key]=clone(copy);});toast('Forma copiada a las cuatro etapas.');};
$('new').onclick=()=>$('new-dialog').showModal();
for(let i=1;i<=4;i++){const b=document.createElement('button');b.type='button';const icon=document.createElement('span');icon.textContent=['∿','♧','♉','◁'][i-1];b.append(icon,document.createTextNode(['Lagarto','Perro','Brumacerno','Velkar'][i-1]));b.onclick=()=>{$('new-dialog').close();load(`builtin:${i}`,true);};$('templates').append(b);}
$('import').onclick=()=>$('file-input').click();$('file-input').onchange=async()=>{const file=$('file-input').files[0];if(!file)return;try{if(file.size>2097152)throw Error('El archivo supera 2 MiB.');const parsed=JSON.parse(await file.text());const validated=await api('/api/validate',{document:parsed});if(dirty&&!confirm('¿Descartar los cambios sin guardar e importar este archivo?'))return;doc=validated;source=null;dirty=true;undo=[];redo=[];index=0;part='body';revision++;fitNext=true;render();renderLibrary();preview();toast('Receta importada y validada.');}catch(e){showError(e.message);}finally{$('file-input').value='';}};
$('export').onclick=async()=>{if(!doc)return;try{const validated=await api('/api/validate',{document:doc});const blob=new Blob([JSON.stringify(validated,null,2)+'\n'],{type:'application/json'});const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download=slug(doc.recipe.name)+'.creature.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}catch(e){showError(e.message);}};
window.addEventListener('keydown',e=>{if(e.target.matches('input,textarea,select'))return;if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z'){e.preventDefault();e.shiftKey?history(redo,undo):history(undo,redo);}if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='s'){e.preventDefault();openSave();}});
window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});
try{viewport=new Viewport($('canvas'),positionHandles);}catch(e){showError(e.message);$('preview-status').textContent='Vista 3D no disponible';}
try{await refreshLibrary();await load('builtin:2');}catch(e){showError(e.message);$('preview-status').textContent='No se pudo iniciar el editor';}
function editorState(){return{document:doc,source,dirty,view:{stage,part,index,tab,camera:cameraSelection,quality:$('quality').value}};}
function applySharedState(state){
    const geometryChanged=JSON.stringify(doc)!==JSON.stringify(state.document)||stage!==state.view.stage||$('quality').value!==state.view.quality;
    const cameraChanged=cameraSelection!==state.view.camera;
    fitNext=fitNext||source!==state.source||stage!==state.view.stage;
    doc=clone(state.document);source=state.source;dirty=state.dirty;
    stage=state.view.stage;part=state.view.part;index=state.view.index;tab=state.view.tab;
    cameraSelection=state.view.camera;$('quality').value=state.view.quality;
    for(const key of stages)if(doc.recipe[key].limbs.length)limbDefaults[key]=clone(doc.recipe[key].limbs);
    revision++;undo=[];redo=[];render();renderLibrary();if(source&&!entries.some(e=>e.key===source))refreshLibrary();
    if(cameraChanged)viewport?.view(cameraSelection);if(geometryChanged)preview();
}
try{await connectAgent(editorState,applySharedState,showError);}catch(e){showError('No se pudo conectar la sesión para agentes: '+e.message);}
