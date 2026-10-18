// Generated from scripts/CharWeaveStyleLibrary.jsx by build.sh.
static const char kStyleLibraryScript[] = R"CW_JS(/*
 * CharWeave managed style library v1. ES3 / ExtendScript compatible.
 * UI-only: never called by a PF render callback. Saved expressions and a normal
 * PF layer checkout are the entire render dependency; there are no sidecar images.
 */
(function (global) {
    var VERSION = '0.4.4-2';
    if (global.CWStyleLibrary && global.CWStyleLibrary.version === VERSION) return;
    if (global.CWStyleLibrary && global.CWStyleLibrary.stop) global.CWStyleLibrary.stop();
    var TAG = 'CW_STYLE_LIBRARY:v1', OWN = '// CharWeave style layout v1';
    var GRID = 8, TILE = 128, SIZE = GRID * TILE, LIMIT = GRID * GRID;
    var api = {version:VERSION, enabled:true, task:0, pendingTask:0, pending:[],
               lastError:'', lastCount:0, panel:null, status:null, signatures:{}};
    // New disk IDs are appended; the original 1..33 parameter IDs never change.
    var PARAM = {source:34, create:35, layer:36, manage:37, color:38, sort:39};

    function isLibrary(comp) {
        try { return comp instanceof CompItem && comp.comment.indexOf(TAG) === 0; }
        catch (e) { return false; }
    }
    function findComp(id) {
        if (!app.project) return null;
        for (var i=1; i<=app.project.numItems; ++i) {
            var item = app.project.item(i);
            if (item instanceof CompItem && item.id === id) return item;
        }
        return null;
    }
    function context(request) {
        var comp = findComp(request.compId), layer = null;
        if (!comp) throw new Error('The effect composition was closed or deleted.');
        for (var i=1; i<=comp.numLayers; ++i) {
            var candidate = comp.layer(i);
            if (typeof candidate.id !== 'undefined' && candidate.id === request.layerId) {
                layer=candidate; break;
            }
        }
        // AEGP and scripting use the same persistent layer ID on AE 22+.
        // Never fall back to a different layer after the queued UI action.
        if (!layer) throw new Error('The effect layer changed. Click the button again.');
        var effects = layer.property('ADBE Effect Parade');
        var effect = effects ? effects.property(request.effectIndex+1) : null;
        if (!effect || effect.matchName !== 'CW CharWeave Native' || effect.numProperties < PARAM.sort)
            throw new Error('The CharWeave effect changed. Click the button again.');
        return {comp:comp, layer:layer, effect:effect};
    }
    function linkedLibrary(ctx) {
        var index = ctx.effect.property(PARAM.layer).value;
        if (index < 1 || index > ctx.comp.numLayers) return null;
        var layer = ctx.comp.layer(index);
        return isLibrary(layer.source) ? layer.source : null;
    }
    function styleLayer(layer) {
        // These are layout controls, not independent image/shape/text styles.
        // AE host subclasses (ShapeLayer/TextLayer) are not necessarily instanceof AVLayer.
        return layer && layer.hasVideo === true && !layer.nullLayer &&
               !layer.guideLayer && !layer.adjustmentLayer;
    }
    function hasCharWeave(layer) {
        var effects = layer.property('ADBE Effect Parade');
        if (!effects) return false;
        for (var i=1; i<=effects.numProperties; ++i)
            if (effects.property(i).matchName === 'CW CharWeave Native') return true;
        return false;
    }
    function propertyPlan(layer, slot) {
        if (layer.parent || layer.threeDLayer)
            throw new Error('Precompose parented / 3D style: ' + layer.name);
        if (hasCharWeave(layer))
            throw new Error('Do not put CharWeave inside its own style library: ' + layer.name);
        var transform=layer.property('ADBE Transform Group');
        var anchor=transform.property('ADBE Anchor Point');
        var position=transform.property('ADBE Position');
        var scale=transform.property('ADBE Scale');
        if (position.dimensionsSeparated)
            throw new Error('Precompose a style with separated Position: ' + layer.name);
        var x=(slot%GRID+.5)*TILE, y=(Math.floor(slot/GRID)+.5)*TILE;
        var isGeometry=layer.matchName==='ADBE Vector Layer' || layer.matchName==='ADBE Text Layer';
        // Footage/precomp styles use their native image rectangle; sourceRectAtTime
        // is reserved for vector/text geometry to avoid host evaluation edge cases.
        var bounds=isGeometry ? 'var r=sourceRectAtTime(time,true); ' :
            'var r={left:0,top:0,width:width,height:height}; ';
        var expressions=[
            OWN+'\n'+bounds+'[r.left+r.width/2,r.top+r.height/2];',
            OWN+'\n['+x+','+y+'];',
            OWN+'\n'+bounds+'var a=degreesToRadians(transform.rotation); '+
                'var w=Math.abs(r.width*Math.cos(a))+Math.abs(r.height*Math.sin(a)); '+
                'var h=Math.abs(r.width*Math.sin(a))+Math.abs(r.height*Math.cos(a)); '+
                'var f=100*'+(TILE-24)+'/Math.max(1,w,h); [f,f];'
        ];
        var props=[anchor,position,scale], result=[];
        for (var i=0; i<props.length; ++i) {
            var prop=props[i];
            if (!prop || !prop.canSetExpression)
                throw new Error('Unsupported style transform: ' + layer.name);
            if (prop.expression && prop.expression.indexOf(OWN)!==0)
                throw new Error('Keep your custom transform expressions by precomposing: ' + layer.name);
            if (prop.expression !== expressions[i] || !prop.expressionEnabled) {
                if (layer.locked) throw new Error('Unlock the new/reordered style: ' + layer.name);
                result.push({property:prop, expression:expressions[i],
                             oldExpression:prop.expression, oldEnabled:prop.expressionEnabled});
            }
        }
        return result;
    }
    function makePlan(comp) {
        if (!isLibrary(comp)) throw new Error('Use Create style precomp; untagged comps are not modified.');
        if (comp.width!==SIZE || comp.height!==SIZE || Math.abs(comp.pixelAspect-1)>.0001)
            throw new Error('Style library must stay 1024 x 1024 with square pixels.');
        var layers=[], plan=[];
        for (var i=1; i<=comp.numLayers; ++i) if (styleLayer(comp.layer(i))) layers.push(comp.layer(i));
        if (layers.length>LIMIT) throw new Error('Maximum 64 styles. Move extra layers to a second library.');
        // Validate every layer before making any change (no half-updated layout).
        for (var n=0; n<layers.length; ++n) {
            var additions=propertyPlan(layers[n],n);
            for (var p=0; p<additions.length; ++p) plan.push(additions[p]);
        }
        return {changes:plan,count:layers.length};
    }
    function applyPlan(plan) {
        var done=0;
        try {
            for (;done<plan.changes.length;++done) {
                var change=plan.changes[done];
                change.property.expression=change.expression;
                change.property.expressionEnabled=true;
            }
        } catch(e) {
            // The current write can have succeeded before expressionEnabled failed.
            for(var i=Math.min(done,plan.changes.length-1);i>=0;--i) {
                try {
                    var old=plan.changes[i];
                    old.property.expression=old.oldExpression;
                    if(old.oldExpression) old.property.expressionEnabled=old.oldEnabled;
                } catch(ignore) {}
            }
            throw e;
        }
        api.lastCount=plan.count;
        return plan.changes.length>0;
    }
    function topology(comp) {
        var parts=[comp.width,comp.height,comp.pixelAspect];
        for(var i=1;i<=comp.numLayers;++i) {
            var layer=comp.layer(i);
            if(styleLayer(layer)) parts.push(layer.id);
        }
        return parts.join('|');
    }
    function sync(comp, undo) {
        var plan=makePlan(comp);
        if (!plan.changes.length) {api.lastCount=plan.count;api.signatures[comp.id]=topology(comp);return false;}
        if (undo) app.beginUndoGroup('CharWeave: arrange new layer styles');
        try {var changed=applyPlan(plan);api.signatures[comp.id]=topology(comp);return changed;}
        finally {if(undo) app.endUndoGroup();}
    }
    function shape(comp,name,ellipse,size,color) {
        var layer=comp.layers.addShape();layer.name=name;
        var group=layer.property('ADBE Root Vectors Group').addProperty('ADBE Vector Group');
        var contents=group.property('ADBE Vectors Group');
        contents.addProperty(ellipse?'ADBE Vector Shape - Ellipse':'ADBE Vector Shape - Rect')
                .property(ellipse?'ADBE Vector Ellipse Size':'ADBE Vector Rect Size').setValue(size);
        // addProperty invalidates indexed-group children; reacquire the group.
        group=layer.property('ADBE Root Vectors Group').property(1);
        group.property('ADBE Vectors Group').addProperty('ADBE Vector Graphic - Fill')
             .property('ADBE Vector Fill Color').setValue(color);
        return layer;
    }
    function create(request) {
        var ctx=context(request), existing=linkedLibrary(ctx);
        if (existing) {sync(existing,true);existing.openInViewer();api.start();return existing;}
        if (ctx.layer.locked) throw new Error('Unlock the target layer first.');
        app.beginUndoGroup('CharWeave: create layer style library');
        var library=null, helper=null;
        var oldSource=ctx.effect.property(PARAM.source).value;
        var oldColor=ctx.effect.property(PARAM.color).value;
        var oldIndex=ctx.effect.property(PARAM.layer).value;
        var oldLayer=oldIndex>0 && oldIndex<=ctx.comp.numLayers ? ctx.comp.layer(oldIndex) : null;
        try {
            library=app.project.items.addComp('CharWeave Styles - '+ctx.layer.name,
                SIZE,SIZE,1,Math.max(3,ctx.comp.duration),ctx.comp.frameRate);
            library.comment=TAG+'\nEach layer is one style. CharWeave owns Anchor/Position/Scale.'+
                '\nUse a precomp to preserve internal transforms; maximum 64 styles.';
            if (ctx.comp.parentFolder) library.parentFolder=ctx.comp.parentFolder;
            shape(library,'03 - square',false,[78,78],[.98,.36,.15]);
            shape(library,'02 - circle',true,[60,60],[.15,.7,1]);
            var text=library.layers.addText('A');text.name='01 - text (replace me)';
            var document=text.property('ADBE Text Properties').property('ADBE Text Document').value;
            document.font='Helvetica';document.fontSize=90;document.applyFill=true;
            document.fillColor=[1,.92,.36];
            text.property('ADBE Text Properties').property('ADBE Text Document').setValue(document);
            sync(library,false);
            helper=ctx.comp.layers.add(library);
            helper.name='[CW styles] '+library.name;helper.selected=false;
            helper.comment='CW_STYLE_SOURCE:v1';helper.enabled=false;helper.audioEnabled=false;
            helper.guideLayer=true;helper.shy=true;
            helper.startTime=ctx.comp.displayStartTime;
            helper.inPoint=ctx.comp.displayStartTime;
            helper.outPoint=ctx.comp.displayStartTime+ctx.comp.duration;
            // Layer references follow the layer when new layers are inserted/reordered.
            ctx.effect.property(PARAM.layer).setValue(helper.index);
            ctx.effect.property(PARAM.source).setValue(2);
            ctx.effect.property(PARAM.color).setValue(2);
        } catch(error) {
            // Roll back only items created by this invocation, never existing art.
            try {if(helper) helper.remove();} catch(ignore) {}
            try {ctx.effect.property(PARAM.layer).setValue(oldLayer ? oldLayer.index : 0);} catch(ignore) {}
            try {ctx.effect.property(PARAM.source).setValue(oldSource);} catch(ignore) {}
            try {ctx.effect.property(PARAM.color).setValue(oldColor);} catch(ignore) {}
            try {if(library) library.remove();} catch(ignore) {}
            throw error;
        } finally {app.endUndoGroup();}
        library.openInViewer();api.start();
        return library;
    }
    function canRun() {
        try {return app.project && !app.isRenderEngine && !app.project.renderQueue.rendering;}
        catch(e) {return false;}
    }
    api.sync=sync;api.create=create;api.isLibrary=isLibrary;api.makePlan=makePlan;
    api.refreshAll=function(force){
        if (!canRun()) return;
        var problems=[];
        for(var i=1;i<=app.project.numItems;++i) {
            var comp=app.project.item(i);
            if(isLibrary(comp)) {
                try {if(force!==false || api.signatures[comp.id]!==topology(comp)) sync(comp,true);} catch(e) {problems.push(comp.name+': '+e.toString());}
            }
        }
        api.lastError=problems.join('\n');
        if(api.status) api.status.text=api.lastError || ('Ready - '+api.lastCount+' styles (max 64).');
    };
    api.tick=function(){
        api.task=0;
        if(!api.enabled || app.isRenderEngine) return;
        try {api.refreshAll(false);} catch(e) {api.lastError=e.toString();}
        api.task=app.scheduleTask('$.global.CWStyleLibrary.tick()',1000,false);
    };
    api.start=function(){
        if(!api.enabled || api.task || app.isRenderEngine || !app.project) return;
        // Ordinary Custom text projects need no background task at all.
        for(var i=1;i<=app.project.numItems;++i) {
            if(isLibrary(app.project.item(i))) {
                api.task=app.scheduleTask('$.global.CWStyleLibrary.tick()',1000,false);
                return;
            }
        }
    };
    api.stop=function(){
        if(api.task) {try {app.cancelTask(api.task);} catch(e) {} api.task=0;}
        if(api.pendingTask) {try {app.cancelTask(api.pendingTask);} catch(e) {} api.pendingTask=0;}
    };
    api.enqueue=function(request){
        api.pending.push(request);
        if(!api.pendingTask)
            api.pendingTask=app.scheduleTask('$.global.CWStyleLibrary.flush()',100,false);
    };
    api.flush=function(){
        api.pendingTask=0;
        if (!canRun()) {api.pending=[];return;}
        var requests=api.pending;api.pending=[];
        for(var i=0;i<requests.length;++i) {
            try {
                if(requests[i].action===1) create(requests[i]);
                else {
                    var ctx=context(requests[i]), library=linkedLibrary(ctx);
                    if(!library) throw new Error('No managed library. Click Create style precomp first.');
                    sync(library,true);library.openInViewer();api.showManager();api.start();
                }
            } catch(e) {api.lastError=e.toString();alert('CharWeave styles\n'+api.lastError);}
        }
    };
    api.runForSelection=function(action){
        var comp=app.project.activeItem;
        if(!(comp instanceof CompItem) || comp.selectedLayers.length!==1)
            throw new Error('Select exactly one layer with CharWeave in its effect composition.');
        var owner=comp.selectedLayers[0], effects=owner.property('ADBE Effect Parade'), found=[];
        for(var i=1;i<=effects.numProperties;++i)
            if(effects.property(i).matchName==='CW CharWeave Native') found.push(i);
        if(found.length!==1)
            throw new Error('Select one target layer with exactly one CharWeave instance.');
        var request={compId:comp.id,layerId:owner.id,effectIndex:found[0]-1};
        if(action===1) return create(request);
        var ctx=context(request),library=linkedLibrary(ctx);
        if(!library)throw new Error('No managed library; create one first.');
        sync(library,true);library.openInViewer();api.showManager();api.start();return library;
    };
    api.createForSelection=function(){return api.runForSelection(1);};
    api.showManager=function(host){
        if(api.panel && !host) {api.panel.show();return api.panel;}
        var panel=host || new Window('palette','CharWeave - Layer Styles');
        panel.orientation='column';panel.alignChildren=['fill','top'];
        var instructions=panel.add('statictext',undefined,
            'One shape/text/image/precomp layer = one style.\n'+
            'Anchor / Position / Scale are managed automatically.\n'+
            'Precompose to preserve your own transform animation.',{multiline:true});
        instructions.preferredSize=[410,60];
        var createButton=panel.add('button',undefined,'Create / open styles for selected CharWeave layer');
        createButton.onClick=function(){try {api.createForSelection();} catch(e) {alert(e.toString());}};
        var toggle=panel.add('checkbox',undefined,'Auto-arrange new / reordered styles (1 second)');
        toggle.value=api.enabled;
        toggle.onClick=function(){api.enabled=toggle.value;if(api.enabled) {api.signatures={};api.start();}else api.stop();};
        var refresh=panel.add('button',undefined,'Refresh managed libraries now');
        refresh.onClick=function(){api.refreshAll();};
        api.status=panel.add('statictext',undefined,'Ready - max 64 styles.',{multiline:true});
        api.status.preferredSize=[410,55];
        panel.onClose=function(){api.panel=null;api.status=null;};
        panel.onResizing=panel.onResize=function(){this.layout.resize();};
        panel.layout.layout(true);api.panel=panel;
        if(!host) {panel.center();panel.show();}
        api.start();return panel;
    };
    global.CWStyleLibrary=api;
    // Loading this file defines the API only. No project is changed or timer
    // started until the native UI/manager explicitly starts it.
})($.global);
)CW_JS";
