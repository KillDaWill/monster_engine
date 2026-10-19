// Sincronización pública del taller: evita que una revisión antigua sobrescriba otra nueva.
export async function connectAgent(read, apply, error){
    async function request(path,body){const response=await fetch(path,body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{});const result=await response.json();if(!response.ok){const failure=Error(result.error);failure.status=response.status;throw failure;}return result;}
    let state=await request('/api/agent/state');apply(state);let revision=state.revision;
    let acknowledged=JSON.stringify(read()),running=false,paused=false;
    window.monsterEditor={state:()=>request('/api/agent/state'),schema:()=>request('/api/agent/schema'),
        action:async action=>{const current=await request('/api/agent/state');return request('/api/agent/action',{...action,expected_revision:current.revision});},
        save:async options=>{const current=await request('/api/agent/state');return request('/api/agent/save',{...options,expected_revision:current.revision});}};
    const badge=document.createElement('button');badge.id='agent-state';badge.title='Estado compartido con agentes. Pulsa para recargar la sesión.';badge.textContent='IA · r'+revision;document.querySelector('.header-actions').prepend(badge);
    badge.onclick=async()=>{if(!confirm('¿Recargar la sesión compartida y descartar cambios locales pendientes?'))return;const next=await request('/api/agent/state');apply(next);revision=next.revision;acknowledged=JSON.stringify(read());paused=false;badge.textContent='IA · r'+revision;};
    setInterval(async()=>{if(running||paused)return;running=true;
        try{const snapshot=read(),encoded=JSON.stringify(snapshot);
            if(encoded!==acknowledged){
                if(JSON.stringify(snapshot.document)!==JSON.stringify(state.document)||snapshot.source!==state.source||snapshot.dirty!==state.dirty){
                    state=await request('/api/agent/action',{action:'replace',document:snapshot.document,source:snapshot.source,dirty:snapshot.dirty,expected_revision:revision});revision=state.revision;
                }
                if(JSON.stringify(snapshot.view)!==JSON.stringify(state.view)){state=await request('/api/agent/action',{action:'view',view:snapshot.view,expected_revision:revision});revision=state.revision;}
                acknowledged=encoded;
            }else{const next=await request('/api/agent/state?since='+revision);if(next.document&&JSON.stringify(read())===acknowledged){state=next;revision=next.revision;apply(next);acknowledged=JSON.stringify(read());}}
            badge.textContent='IA · r'+revision;
        }catch(failure){if(failure.status===409){paused=true;badge.textContent='IA · conflicto';error(failure.message+' Pulsa «IA · conflicto» para recargar la sesión.');}
            else if(failure.status!==422){error(failure.message);}}
        finally{running=false;}
    },400);
}
