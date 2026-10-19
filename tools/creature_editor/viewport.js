// Visor WebGL2 de las mallas producidas por el motor; sin geometría de sustitución.
const sub=(a,b)=>a.map((x,i)=>x-b[i]);
const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const unit=a=>{const n=Math.hypot(...a)||1;return a.map(x=>x/n);};
function multiply(a,b){const c=new Float32Array(16);for(let j=0;j<4;j++)for(let i=0;i<4;i++)for(let k=0;k<4;k++)c[j*4+i]+=a[k*4+i]*b[j*4+k];return c;}
function camera(eye,target){const z=unit(sub(eye,target)),x=unit(cross([0,1,0],z)),y=cross(z,x);return new Float32Array([x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,-dot(x,eye),-dot(y,eye),-dot(z,eye),1]);}
function perspective(aspect,near,far){const f=1/Math.tan(.36);return new Float32Array([f/aspect,0,0,0,0,f,0,0,0,0,(far+near)/(near-far),-1,0,0,2*far*near/(near-far),0]);}
export class Viewport{
    constructor(canvas,onFrame){
        this.canvas=canvas;this.gl=canvas.getContext('webgl2',{antialias:true,alpha:false});
        if(!this.gl)throw Error('El editor necesita WebGL 2. Activa la aceleración gráfica del navegador.');
        const gl=this.gl;
        const vertex=`#version 300 es
        in vec3 position;in vec3 normal;in vec4 color;uniform mat4 mvp;out vec3 N;out vec4 C;out vec3 P;
        void main(){P=position;N=normal;C=color;gl_Position=mvp*vec4(position,1.);}`;
        const fragment=`#version 300 es
        precision highp float;in vec3 N;in vec4 C;in vec3 P;uniform bool floorMode;uniform float radius;uniform vec3 center;out vec4 result;
        void main(){if(floorMode){vec2 p=P.xz;vec2 grid=abs(fract(p-.5)-.5)/fwidth(p);float line=1.-min(min(grid.x,grid.y),1.);float fade=1.-smoothstep(radius*.5,radius*4.,length(p-center.xz));float shade=exp(-dot((p-center.xz)/radius,(p-center.xz)/radius)*5.);result=vec4(vec3(.107,.19,.196)+line*fade*.035-shade*.025,1.);}
        else{vec3 n=normalize(N);float light=max(dot(n,normalize(vec3(-.6,1.,.8))),0.);float rim=pow(max(dot(n,normalize(vec3(.5,.3,-.7))),0.),3.);vec3 color=C.rgb*(.40+light*.67)+vec3(.12,.18,.15)*rim;result=vec4(pow(color,vec3(.88)),1.);}}`;
        const shader=(type,source)=>{const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;};
        this.program=gl.createProgram();gl.attachShader(this.program,shader(gl.VERTEX_SHADER,vertex));gl.attachShader(this.program,shader(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(this.program);
        if(!gl.getProgramParameter(this.program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(this.program));
        this.mvpLocation=gl.getUniformLocation(this.program,'mvp');this.floorLocation=gl.getUniformLocation(this.program,'floorMode');this.meshes=[];this.center=[0,0,0];this.target=[0,0,0];this.radius=5;this.distance=22;this.yaw=.95;this.pitch=.2;this.nodes=[];this.onFrame=onFrame;this.turn=false;
        canvas.addEventListener('contextmenu',e=>e.preventDefault());
        canvas.addEventListener('pointerdown',e=>{this.pointer={x:e.clientX,y:e.clientY,button:e.button};canvas.setPointerCapture(e.pointerId);});
        canvas.addEventListener('pointermove',e=>{if(!this.pointer)return;const dx=e.clientX-this.pointer.x,dy=e.clientY-this.pointer.y;
            if(this.pointer.button===2){const f=this.distance/canvas.clientHeight;this.target[0]-=dx*Math.cos(this.yaw)*f;this.target[2]+=dx*Math.sin(this.yaw)*f;this.target[1]+=dy*f;}
            else{this.yaw-=dx*.008;this.pitch=Math.min(1.4,Math.max(-.3,this.pitch+dy*.008));}
            this.pointer.x=e.clientX;this.pointer.y=e.clientY;});
        const stop=()=>{this.pointer=null;};canvas.addEventListener('pointerup',stop);canvas.addEventListener('pointercancel',stop);
        canvas.addEventListener('wheel',e=>{e.preventDefault();this.distance=Math.min(this.radius*20,Math.max(this.radius*.65,this.distance*Math.exp(e.deltaY*.001)));},{passive:false});
        this.animate=this.animate.bind(this);requestAnimationFrame(this.animate);
    }
    buffer(vertices,indices){const gl=this.gl,vao=gl.createVertexArray();gl.bindVertexArray(vao);const vbo=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,vbo);gl.bufferData(gl.ARRAY_BUFFER,vertices,gl.STATIC_DRAW);
        for(const [name,size,offset] of [['position',3,0],['normal',3,12],['color',4,24]]){const loc=gl.getAttribLocation(this.program,name);gl.enableVertexAttribArray(loc);gl.vertexAttribPointer(loc,size,gl.FLOAT,false,40,offset);}
        const ebo=gl.createBuffer();gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,ebo);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,indices,gl.STATIC_DRAW);return{vao,vbo,ebo,count:indices.length};}
    load(data,fit=false){const gl=this.gl;const size=new DataView(data).getUint32(0,true),meta=JSON.parse(new TextDecoder().decode(new Uint8Array(data,4,size)));let offset=4+size;const meshes=[];const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];let triangles=0,verticesCount=0;
        for(const m of meta.meshes){const length=m.vertices*40;const vertices=new Float32Array(data.slice(offset,offset+length));offset+=length;const indices=new Uint32Array(data.slice(offset,offset+m.indices*4));offset+=m.indices*4;
            for(let i=0;i<vertices.length;i+=10)for(let k=0;k<3;k++){min[k]=Math.min(min[k],vertices[i+k]);max[k]=Math.max(max[k],vertices[i+k]);}
            meshes.push(this.buffer(vertices,indices));triangles+=indices.length/3;verticesCount+=m.vertices;}
        if(!meshes.length)throw Error('La criatura no produjo una superficie visible.');
        for(const m of [...this.meshes,...(this.floor?[this.floor]:[])]){gl.deleteVertexArray(m.vao);gl.deleteBuffer(m.vbo);gl.deleteBuffer(m.ebo);}
        this.meshes=meshes;this.nodes=meta.nodes;this.center=min.map((x,i)=>(x+max[i])/2);this.radius=Math.max(...max.map((x,i)=>x-min[i]))*.6;
        const r=this.radius*8,y=min[1]-.03;
        this.floor=this.buffer(new Float32Array([-r,y,-r,0,1,0,1,1,1,1,r,y,-r,0,1,0,1,1,1,1,r,y,r,0,1,0,1,1,1,1,-r,y,r,0,1,0,1,1,1,1]),new Uint32Array([0,2,1,0,3,2]));
        if(fit||!this.hasModel)this.fit();this.hasModel=true;return{triangles,vertices:verticesCount};
    }
    fit(){this.target=[...this.center];this.distance=this.radius*3.3;}
    view(name){this.yaw=name==='front'?0:name==='side'?Math.PI/2:.95;this.pitch=name==='top'?1.45:name==='perspective'?.2:.08;this.fit();}
    project(point){if(!this.mvp)return null;const q=[0,0,0,0];for(let i=0;i<4;i++)for(let k=0;k<4;k++)q[i]+=this.mvp[k*4+i]*(k===3?1:point[k]);if(q[3]<=0)return null;return[(q[0]/q[3]*.5+.5)*this.canvas.clientWidth,(-q[1]/q[3]*.5+.5)*this.canvas.clientHeight];}
    animate(time){const gl=this.gl,c=this.canvas,dpr=Math.min(window.devicePixelRatio||1,2),w=Math.round(c.clientWidth*dpr),h=Math.round(c.clientHeight*dpr);if(c.width!==w||c.height!==h){c.width=w;c.height=h;}
        if(this.turn&&!this.pointer)this.yaw+=Math.min((time-(this.time||time))/1000,.1)*.22;this.time=time;
        const eye=[this.target[0]+Math.sin(this.yaw)*Math.cos(this.pitch)*this.distance,this.target[1]+Math.sin(this.pitch)*this.distance,this.target[2]+Math.cos(this.yaw)*Math.cos(this.pitch)*this.distance];
        this.mvp=multiply(perspective(w/Math.max(h,1),Math.max(.001,this.radius*.002),this.radius*100),camera(eye,this.target));
        gl.viewport(0,0,w,h);gl.clearColor(.107,.19,.196,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.enable(gl.DEPTH_TEST);gl.useProgram(this.program);gl.uniformMatrix4fv(this.mvpLocation,false,this.mvp);gl.uniform1f(gl.getUniformLocation(this.program,'radius'),this.radius);gl.uniform3fv(gl.getUniformLocation(this.program,'center'),this.center);
        if(this.floor){gl.uniform1i(this.floorLocation,1);gl.bindVertexArray(this.floor.vao);gl.drawElements(gl.TRIANGLES,this.floor.count,gl.UNSIGNED_INT,0);}
        gl.uniform1i(this.floorLocation,0);for(const m of this.meshes){gl.bindVertexArray(m.vao);gl.drawElements(gl.TRIANGLES,m.count,gl.UNSIGNED_INT,0);}this.onFrame?.();requestAnimationFrame(this.animate);
    }
}
