/* Deterministic mock tests: these validate JSX logic, not AE suite/UI behavior. */
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
let id=10,writes=0,undo=[],tasks=new Map(),nextTask=1,alerts=[];
class Prop {
  constructor(value=0){this.value=value;this._expression='';this.expressionEnabled=false;this.canSetExpression=true;this.dimensionsSeparated=false;this.children=[];this.map={};}
  get expression(){return this._expression;}
  set expression(v){this._expression=v;writes++;}
  setValue(v){this.value=v;writes++;}
  property(key){if(typeof key==='number')return this.children[key-1];if(!this.map[key])this.map[key]=new Prop();return this.map[key];}
  addProperty(key){let p=new Prop();p.matchName=key;this.children.push(p);return p;}
  get numProperties(){return this.children.length;}
}
class LayerRef extends Prop {
  constructor(owner){super();this.owner=owner;this.target=null;}
  setValue(index){this.target=this.owner.comp.layer(index);writes++;}
  get value(){return this.target?this.target.index:0;}
  set value(v){} // super() initializer
}
class Effect extends Prop {
  constructor(owner){super();this.matchName='CW CharWeave Native';for(let i=0;i<39;i++)this.children.push(new Prop());this.children[35]=new LayerRef(owner);}
}
class AVLayer {
  constructor(comp,name='style'){this.id=++id;this.comp=comp;this.name=name;this.hasVideo=true;this.nullLayer=false;this.guideLayer=false;this.adjustmentLayer=false;this.parent=null;this.threeDLayer=false;this.locked=false;this.source=null;this.map={};this.map['ADBE Effect Parade']=new Prop();this.map['ADBE Transform Group']=new Prop();this.map['ADBE Text Properties']=new Prop();this.property('ADBE Text Properties').property('ADBE Text Document').value={};}
  get index(){return this.comp._layers.indexOf(this)+1;}
  remove(){this.comp._layers.splice(this.comp._layers.indexOf(this),1);}
  property(name){if(!this.map[name])this.map[name]=new Prop();return this.map[name];}
}
class ShapeLayer {
  constructor(comp,name){Object.assign(this,new AVLayer(comp,name));}
  property(name){return AVLayer.prototype.property.call(this,name);}
  get index(){return this.comp._layers.indexOf(this)+1;}
  remove(){this.comp._layers.splice(this.comp._layers.indexOf(this),1);}
}
class TextLayer extends ShapeLayer {}
class CameraLayer {constructor(comp){this.id=++id;this.comp=comp;this.name='camera';}}
class CompItem {
  constructor(name='comp',w=640,h=360,aspect=1,duration=3,fps=24){this.id=++id;this.name=name;this.width=w;this.height=h;this.pixelAspect=aspect;this.duration=duration;this.frameRate=fps;this.displayStartTime=0;this.comment='';this.parentFolder={};this._layers=[];this.opened=0;
    this.layers={add:(source)=>{let l=this.add('helper');l.source=source;return l;},addShape:()=>this.add('shape',ShapeLayer),addText:(text)=>this.add('text '+text,TextLayer)};}
  add(name,Ctor=AVLayer){let l=new Ctor(this,name);this._layers.unshift(l);return l;}
  layer(i){return this._layers[i-1];}
  get numLayers(){return this._layers.length;}
  get selectedLayers(){return this._layers.filter(l=>l.selected);}
  openInViewer(){this.opened++;app.project.activeItem=this;}
  remove(){project._items.splice(project._items.indexOf(this),1);}
}
const project={_items:[],activeItem:null,renderQueue:{rendering:false},get numItems(){return this._items.length;},item(i){return this._items[i-1];},items:{addComp:(...a)=>{let c=new CompItem(...a);project._items.push(c);return c;}}};
const app={project,isRenderEngine:false,beginUndoGroup:(s)=>undo.push(s),endUndoGroup:()=>{},scheduleTask:(script,delay,repeat)=>{assert.equal(repeat,false);let key=nextTask++;tasks.set(key,{script,delay});return key;},cancelTask:(key)=>tasks.delete(key)};
const global={};const sandbox={$:{global},app,CompItem,AVLayer,alert:(v)=>alerts.push(v),Window:function(){throw new Error('UI is not part of mock tests');}};
vm.createContext(sandbox);
const source=fs.readFileSync(require('node:path').join(__dirname,'../scripts/CharWeaveStyleLibrary.jsx'),'utf8');
vm.runInContext(source,sandbox);
const api=global.CWStyleLibrary;
assert.equal(tasks.size,0,'loading script silently starts tasks');
const ownerComp=project.items.addComp('Owner');let owner=ownerComp.add('target');
let fx=new Effect(owner);owner.property('ADBE Effect Parade').children.push(fx);
const request={compId:ownerComp.id,layerId:owner.id,effectIndex:0,action:1};
function executeTask(key){let task=tasks.get(key);assert(task,'missing scheduled task');tasks.delete(key);vm.runInContext(task.script,sandbox);}
api.enqueue(request);assert.equal(project.numItems,1,'project changed during PF callback');
executeTask(api.pendingTask);
assert.equal(alerts.length,0,alerts.join('\n'));
assert.equal(project.numItems,2);const library=project.item(2);
assert(api.isLibrary(library));assert.equal(library.width,1024);assert.equal(library.numLayers,3);
assert(library.layer(1) instanceof TextLayer && !(library.layer(1) instanceof AVLayer));
assert(library.layer(2) instanceof ShapeLayer && !(library.layer(2) instanceof AVLayer));
assert(library.layer(2).property('ADBE Transform Group').property('ADBE Position').expression.includes('CharWeave'),'host-style subclass was skipped');
assert.equal(fx.property(34).value,2);assert.equal(fx.property(38).value,2);
let helper=ownerComp.layer(fx.property(36).value);assert.equal(helper.source,library);
assert.equal(helper.enabled,false);assert.equal(helper.guideLayer,true);
assert.equal(ownerComp.numLayers,2);assert.equal(api.task>0,true);
const prior=project.numItems;api.create(request);assert.equal(project.numItems,prior,'double click creates duplicate libraries');
console.log('PASS deferred create / exact owner / source reference / hidden guide / reuse');

let photo=library.add('PNG/photo'), shape=library.add('shape');
executeTask(api.task);assert.equal(api.lastCount,5);
assert.equal(shape.property('ADBE Transform Group').property('ADBE Position').expression.endsWith('[64,64];'),true);
assert.equal(photo.property('ADBE Transform Group').property('ADBE Position').expression.endsWith('[192,64];'),true);
assert.equal(photo.property('ADBE Transform Group').property('ADBE Scale').expression.includes('width:width,height:height'),true);
let previousWrites=writes,previousUndo=undo.length;executeTask(api.task);
assert.equal(writes,previousWrites,'idle resyncs untouched expressions');assert.equal(undo.length,previousUndo,'idle poll pollutes undo');
// Undoing the last automatic layout must not be immediately counteracted.
photo.property('ADBE Transform Group').property('ADBE Position').expression='';previousWrites=writes;
executeTask(api.task);assert.equal(writes,previousWrites,'automatic layout fights undo');
api.refreshAll();assert(photo.property('ADBE Transform Group').property('ADBE Position').expression.includes('CharWeave'));
library._layers.reverse();executeTask(api.task);
assert.equal(library.layer(1).property('ADBE Transform Group').property('ADBE Position').expression.endsWith('[64,64];'),true);
let duplicate=library.add('duplicated');duplicate.map['ADBE Transform Group'].map['ADBE Position']=new Prop();
executeTask(api.task);assert.equal(api.lastCount,6);
console.log('PASS added images/shapes / reorder / duplicates / no-op / undo-friendly watcher');

const outside=project.items.addComp('User comp');let untouched=outside.add('User layer');
let nullLayer=library.add('null control');nullLayer.nullLayer=true;
let guide=library.add('guide');guide.guideLayer=true;
let camera=new CameraLayer(library);library._layers.unshift(camera);
executeTask(api.task);assert.equal(api.lastCount,6);
assert.equal(untouched.property('ADBE Transform Group').property('ADBE Position').expression,'');
assert.equal(nullLayer.property('ADBE Transform Group').property('ADBE Position').expression,'');
app.project.renderQueue.rendering=true;let addedDuringRender=library.add('pending');previousWrites=writes;
executeTask(api.task);assert.equal(writes,previousWrites,'watcher mutates during render');
app.project.renderQueue.rendering=false;executeTask(api.task);assert.equal(api.lastCount,7);
api.enabled=false;api.stop();assert.equal(tasks.size,0);api.start();assert.equal(tasks.size,0);
api.enabled=true;api.start();let task=api.task;api.start();assert.equal(api.task,task,'multiple timers started');
vm.runInContext(source,sandbox);assert.equal(global.CWStyleLibrary,api,'repeated bootstrap reset watcher');
console.log('PASS unrelated project untouched / non-style layers ignored / render pause / stop / one timer');

const conflict=project.items.addComp('Conflict',1024,1024);conflict.comment='CW_STYLE_LIBRARY:v1';
let clean=conflict.add('clean'),custom=conflict.add('custom');
custom.property('ADBE Transform Group').property('ADBE Position').expression='wiggle(1,10)';
previousWrites=writes;assert.throws(()=>api.sync(conflict,true),/Precomposing|precomposing/);
assert.equal(writes,previousWrites,'partially modified library before validation error');
let full=project.items.addComp('Full',1024,1024);full.comment='CW_STYLE_LIBRARY:v1';
for(let i=0;i<65;i++)full.add('sprite '+i);
previousWrites=writes;assert.throws(()=>api.sync(full,true),/Maximum 64/);assert.equal(writes,previousWrites);
assert.throws(()=>api.sync(outside,true),/untagged/);
// Deleted/stale target never creates anything in the current project.
api.enqueue({compId:999999,layerId:owner.id,effectIndex:0,action:1});executeTask(api.pendingTask);
assert(alerts.pop().includes('closed or deleted'));
let count=project.numItems;
api.enqueue({...request,layerId:999999});executeTask(api.pendingTask);assert.equal(project.numItems,count);
api.stop();
const rollbackComp=project.items.addComp('Rollback owner');const rollbackLayer=rollbackComp.add('Rollback layer');
const rollbackFx=new Effect(rollbackLayer);rollbackLayer.property('ADBE Effect Parade').children.push(rollbackFx);
rollbackFx.property(34).value=1;rollbackFx.property(38).value=1;
let failColor=true;
rollbackFx.property(38).setValue=function(v){if(failColor){failColor=false;throw new Error('mock property failure');}this.value=v;};
let numBefore=project.numItems;
assert.throws(()=>api.create({compId:rollbackComp.id,layerId:rollbackLayer.id,effectIndex:0}),/mock property failure/);
assert.equal(project.numItems,numBefore);assert.equal(rollbackComp.numLayers,1);
assert.equal(rollbackFx.property(34).value,1);assert.equal(rollbackFx.property(36).value,0);assert.equal(rollbackFx.property(38).value,1);
console.log('PASS failed creation rolls back only new helper/library and restores effect settings');
console.log('PASS conflicts and 65th style rejected atomically / untagged comps protected / stale requests');
console.log('ALL JSX LOGIC MOCK TESTS PASSED (not an AE UI integration test)');
