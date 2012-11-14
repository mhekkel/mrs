//---------------------------------------------------------------------
//
//	JavaScript for the Admin pages
//

Admin = {
	timeout: null,
	viewing: 'databanks',

	init: function() {
		Admin.updateStatus();
	},

	changeStatsView: function(view) {
		if (view == Admin.viewing) {
			return;
		}
	
		if (Admin.viewing != null) {
			document.getElementById(Admin.viewing).style.display = 'none';
		}
		Admin.viewing = view;
		document.getElementById(Admin.viewing).style.display = '';
		
		if (Admin.timeout != null) {
			clearTimeout(Admin.timeout);
		}

		Admin.updateStatus();
		
		try {
			sessionStorage.setItem('statsViewing', view);
		} catch (e) {}
	},
	
	updateStatus: function() {
		jQuery.getJSON("ajax/status", { method: Admin.viewing },
			function(data, status) {
				if (status == "success") 
				{
//					switch (Admin.viewing) {
//						case 'files': Admin.updateFiles(data); break;
//						case 'update': Admin.updateUpdate(data); break;
//					}
				}
				Admin.timeout = setTimeout("Admin.updateStatus()", 10000);
			}
		);
	},
	
	editProperty: function(db, prop) {
	    var propNode = document.getElementById(db + '-' + prop);

	    /* only when we're not in edit mode yet */
	    if (propNode.style.display == 'none')
	        return;
	
	    propNode.style.display = 'none';
	
	    var propEdit = document.getElementById('edit-' + db + '-' + prop);
	    propEdit.value = propNode.innerHTML;
	    propEdit.type = 'text';
	    propEdit.select();
	
	    propEdit.onblur = function () {
	        this.type = 'hidden';
	        propNode.style.display = '';
	    }
	
	    propEdit.onkeydown = function(e) {
	        var keyCode = e.keyCode;
	        if (keyCode == 27) {
	            this.type = 'hidden';
	            propNode.style.display = '';
	        }
	        else if (keyCode == 13) {
	            this.type = 'hidden';
				propNode.style.display = '';
	
				jQuery.getJSON("admin/rename", { db: db, prop: prop, value: this.value },
					function(data, status) {
						if (status == "success") {
							$(propNode).text(data.value);
						}
						else {
							alert(status);
						}
					}
				);
	        }
	        return true;
	    }
	}

}

// register a load handler
addLoadEvent(Admin.init);
