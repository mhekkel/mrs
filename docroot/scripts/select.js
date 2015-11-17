//---------------------------------------------------------------------
//
//	Global Selections
//

GlobalSelection = {
	list: null,
	lastClicked: null,
	clickedWithShift: false,
	selectableList: null,

	init: function() {
		GlobalSelection.list = new Array();
	
		try {
			var saved = JSON.parse(sessionStorage.getItem('selectedHits'));
			
			for (i in saved) {
				var cb = document.getElementById(saved[i]);
				if (cb != null) {
					cb.checked = true;
					GlobalSelection.list.push(saved[i]);
				}
			}
	
			GlobalSelection.updateWidget();		
		} catch (e) { }
		
		GlobalSelection.clickedWithShift = false;
		GlobalSelection.selectableList = document.getElementById('selectableList');
	},

	createCheckbox: function(db, id, ch) {
		var checkbox = document.createElement('input');
		checkbox.type = 'checkbox';
		if (ch != null && ch.length > 0) {
			checkbox.id = [db, id, ch].join('/');
		} else {
			checkbox.id = [db, id].join('/');
		}
		
		GlobalSelection.updateCheckbox(checkbox);

		return checkbox;
	},

	add: function(id) {
		var i;
		for (i = 0; i < GlobalSelection.list.length; ++i) {
			if (GlobalSelection.list[i] == id) {
				break;
			}
		}
	
		if (i == GlobalSelection.list.length) {
			GlobalSelection.list.push(id);
		}
	},
		
	remove: function(id) {
		var i;
		for (i = 0; i < GlobalSelection.list.length; ++i) {
			if (GlobalSelection.list[i] == id) {
				break;
			}
		}
	
		if (i < GlobalSelection.list.length) {
			GlobalSelection.list.splice(i, 1);
		}
	},

	clear: function(id) {
		if (GlobalSelection.selectableList != null) {
			var cbs = GlobalSelection.selectableList.getElementsByTagName('input');
			for (var i = 0; i < cbs.length; ++i) {
				var checkbox = cbs[i];
				checkbox.checked = false;
			}
		}
		
		GlobalSelection.list.splice(0, GlobalSelection.list.length);
		GlobalSelection.save();
		GlobalSelection.updateWidget();
	},

	checkboxChanged: function(cb) {
		if (GlobalSelection.clickedWithShift &&
			GlobalSelection.lastClicked != null &&
			GlobalSelection.lastClicked != cb &&
			cb.checked &&
			GlobalSelection.selectableList != null)
		{
			var cbs = GlobalSelection.selectableList.getElementsByTagName('input');
			
			var ix1, ix2;
			for (var i = 0; i < cbs.length; ++i) {
				if (cbs[i] == GlobalSelection.lastClicked) {
					ix1 = i;
				} else if (cbs[i] == cb) {
					ix2 = i;
				}
			}
			
			if (ix1 > ix2) {
				var tmp = ix1; ix1 = ix2; ix2 = tmp;
			}
			
			for (i = ix1; i <= ix2; ++i) {
				cbs[i].checked = true;
				GlobalSelection.add(cbs[i].id);
			}
		}
		else {
			if (cb.checked) {
				GlobalSelection.add(cb.id);
			} else {
				GlobalSelection.remove(cb.id);
			}
		}
		
		GlobalSelection.lastClicked = cb;
	
		// save the new state
		GlobalSelection.save();
		
		// update the list at the bottom
		GlobalSelection.updateWidget();
	},

	updateWidget: function() {
		var widget = document.getElementById('globalSelection');
		if (widget != null) {
			var ul = widget.getElementsByTagName('ul')[0];
			if (ul.children != null) {
				while (ul.children.length > 1) {
					ul.removeChild(ul.children[ul.children.length - 1]);
				}
			}
			if (GlobalSelection.list.length == 0) {
				widget.style.display = 'none';
			} else {
				widget.style.display = '';
				
				var li = null;
				for (var i = 0; i < GlobalSelection.list.length; ++i) {
					li = document.createElement('li');
					ul.appendChild(li);
					if (i >= 5) {
						li.innerHTML = 'and ' + (GlobalSelection.list.length - i) + ' more';
						break;
					} else {
						li.innerHTML = GlobalSelection.list[i];
					}
				}
				
				if (li != null) {
					li.className = 'last';
				}
			}
	
			if (GlobalSelection.list.length < 2) {
				$("input[value='Align']").attr("disabled", true);
			} else {
				$("input[value='Align']").removeAttr("disabled");
			}
		}
	},
	
	updateCheckboxes: function(tbl) {
		var table = document.getElementById(tbl);
		
		GlobalSelection.selectableList = table;
		
		if (table != null) {
			var cbs = table.getElementsByTagName('input');
			for (var i = 0; i < cbs.length; ++i) {
				GlobalSelection.updateCheckbox(cbs[i]);
			}
		}
	},

	updateCheckbox: function(checkbox) {
		jQuery(checkbox).click(function(event) {
			event.stopPropagation();
			GlobalSelection.clickedWithShift = event.shiftKey;
			GlobalSelection.checkboxChanged(event.target);
		});

		for (var i = 0; i < GlobalSelection.list.length; ++i) {
			if (GlobalSelection.list[i] == checkbox.id) {
				checkbox.checked = true;
				break;
			}
		}
	},

	save: function() {
		try {
			if (GlobalSelection.list.length == 0) {
				sessionStorage.removeItem('selectedHits');
			} else {
				sessionStorage.setItem('selectedHits', JSON.stringify(GlobalSelection.list));
			}
		} catch (e) {}
	},

	// what to do with the selected items
	align: function() {
		var form = document.getElementById('alignForm');
		form.seqs.value = GlobalSelection.list.join(';');
		form.submit();
/*		
		// clear options first
		form.seqs.options.length = 0;
		
		GlobalSelection.each(function(db, id){
			form.db.value = db;
			
			var option = document.createElement("option");
			option.text = option.value = id;
			option.selected = true;
			form.id.options[form.id.options.length] = option;
		});
*/
	},

	link: function() {
		var dlog = $("#linkDialog").dialog({
			modal: true,
			resizable: false,
			buttons: {
				"Ok": function() {
					$(this).dialog("close");
					
					var q = '';
					GlobalSelection.each(function(db, id){
						q += db + '/' + id + ' ';
					});

					var form = document.getElementById('queryForm');
					form.db.value = $("#linkToDB").val();
					form.q.value = '[' + q + ']';
					form.submit();
				}
			}
		});
	},

	download: function(format) {
		var dlog = $("#downloadDialog");
		dlog.dialog({
			modal: true,
			resizable: false,
			buttons: {
				"Ok": function() {
					$(this).dialog("close");

					var form = document.getElementById('downloadForm');
					
					// clear options first
					form.id.options.length = 0;
					
					// set the format
					form.format.value = $('#formatForDownloads').val();
					
					GlobalSelection.each(function(db, id, ch){
						form.db.value = db;
						
						if (ch != null) {
							id = id + '/' + ch;
						}

						id = db + '/' + id;

						var option = document.createElement("option");
						option.text = option.value = id;
						option.selected = true;
						form.id.options[form.id.options.length] = option;
					});
					
					form.submit();
				}
			}
		});
	},
	
	each: function(f) {
		var re = /([^/]+)\/([^/]+)(?:\/([^/]+))?/;
		$(GlobalSelection.list).each(function() {
			var m = this.match(re);
			if (m != null) {
				var db = m[1];
				var id = m[2];
				var ch = m[3];
				f(db, id, ch);
			}
		});
	}
}
	
// register a load handler
addLoadEvent(GlobalSelection.init);
