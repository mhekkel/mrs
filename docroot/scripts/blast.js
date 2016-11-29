/*
	This is code specific to the blast pages.
*/

// Blast page code

function validGapValues(matrix) {
	var valid;
	switch (matrix)
	{
		case "BLOSUM45":
			valid = [ [ 13, 3 ], [ 12, 3 ], [ 11, 3 ], [ 10, 3 ], [ 16, 2 ], [ 15, 2 ], [ 14, 2 ], [ 13, 2 ], [ 12, 2 ], [ 19, 1 ], [ 18, 1 ], [ 17, 1 ], [ 16, 1 ] ];
			break;
		case "BLOSUM50":
			valid = [ [ 13, 3 ], [ 12, 3 ], [ 11, 3 ], [ 10, 3 ], [ 9, 3 ], [ 16, 2 ], [ 15, 2 ], [ 14, 2 ], [ 13, 2 ], [ 12, 2 ], [ 19, 1 ], [ 18, 1 ], [ 17, 1 ], [ 16, 1 ], [ 15, 1 ] ];
			break;
		case "BLOSUM62":
			valid = [ [ 11, 2 ], [ 10, 2 ], [ 9, 2 ], [ 8, 2 ], [ 7, 2 ], [ 6, 2 ], [ 13, 1 ], [ 12, 1 ], [ 11, 1 ], [ 10, 1 ], [ 9, 1 ] ];
			break;
		case "BLOSUM80":
			valid = [ [ 25, 2 ], [ 13, 2 ], [ 9, 2 ], [ 8, 2 ], [ 7, 2 ], [ 6, 2 ], [ 11, 1 ], [ 10, 1 ], [ 9, 1 ] ];
			break;
		case "BLOSUM90":
			valid = [ [ 9, 2 ], [ 8, 2 ], [ 7, 2 ], [ 6, 2 ], [ 11, 1 ], [ 10, 1 ], [ 9, 1 ] ];
			break;
		case "PAM250":
			valid = [ [ 15, 3 ], [ 14, 3 ], [ 13, 3 ], [ 12, 3 ], [ 11, 3 ], [ 17, 2 ], [ 16, 2 ], [ 15, 2 ], [ 14, 2 ], [ 13, 2 ], [ 21, 1 ], [ 20, 1 ], [ 19, 1 ], [ 18, 1 ], [ 17, 1 ] ];
			break;
		case "PAM30":
			valid = [ [ 7, 2 ], [ 6, 2 ], [ 5, 2 ], [ 10, 1 ], [ 9, 1 ], [ 8, 1 ] ];
			break;
		case "PAM70":
			valid = [ [ 8, 2 ], [ 7, 2 ], [ 6, 2 ], [ 11, 1 ], [ 10, 1 ], [ 9, 1 ] ];
			break;
	}
	return valid;
}

function updateGapValues()
{
	var blastForm = document.getElementById('blastForm');
	var open, extend = [ 1, 2 ];
	var selectOpen = 11;

	switch (blastForm.matrix.value) {
		case "PAM30":
			open = [ 5, 6, 7, 8, 9, 10 ];
			selectOpen = 10;
			break;
		case "BLOSUM90":
			open = [ 6, 7, 8, 9, 10, 11 ];
			break;
		case "PAM70":
			open = [ 6, 7, 8, 9, 10, 11 ];
			break;
		case "BLOSUM50":
			open = [ 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 ];
			extend = [1, 2, 3 ];
			break;
		case "BLOSUM45":
			open = [ 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 ];
			extend = [1, 2, 3 ];
			break;
		case "BLOSUM80":
			open = [ 6, 7, 8, 9, 10, 11, 13, 25 ];
			break;
		case "PAM250":
			open = [ 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21 ];
			extend = [1, 2, 3 ];
			break;
		case "BLOSUM62":
			open = [ 6, 7, 8, 9, 10, 11, 12, 13 ];
			break;
	}

	with (blastForm.gapOpen) {
		options.length = 0;
		for (o in open) {
			var option = options[options.length] = new Option(open[o], open[o]);
			option.selected = (open[o] == selectOpen);
		}
	}

	with (blastForm.gapExtend) {
		options.length = 0;
		for (e in extend) {
			options[options.length] = new Option(extend[e], extend[e]);
		}
		options[0].selected = true;
	}
	
	gapOpenChanged();
}

function gapOpenChanged() {
	var blastForm = document.getElementById('blastForm');
	var valid = validGapValues(blastForm.matrix.value);
	var gapOpen = blastForm.gapOpen.value;
	var gapExtend = blastForm.gapExtend.value;
	
	var select = blastForm.gapExtend.options.length + 1;
	for (v in valid) {
		if (valid[v][0] != gapOpen)
			continue;
		if (valid[v][1] == gapExtend) {
			select = -1;
			break;
		}
		if (Math.abs(valid[v][1] - gapExtend) < select) {
			select = valid[v][1];
		}
	}
	
	if (select >= 0) {
		with (blastForm.gapExtend) {
			for (o in options) {
				if (options[o].value == select) {
					options[o].selected = true;
				}
			}
		}
	}
}

function gapExtendChanged() {
	var blastForm = document.getElementById('blastForm');
	var valid = validGapValues(blastForm.matrix.value);
	var gapOpen = blastForm.gapOpen.value;
	var gapExtend = blastForm.gapExtend.value;
	
	var select = blastForm.gapOpen.options.length + 1;
	for (v in valid) {
		if (valid[v][1] != gapExtend)
			continue;
		if (valid[v][0] == gapOpen) {
			select = -1;
			break;
		}
		if (Math.abs(valid[v][0] - gapOpen) < select) {
			select = valid[v][0];
		}
	}
	
	if (select >= 0) {
		with (blastForm.gapOpen) {
			for (o in options) {
				if (options[o].value == select) {
					options[o].selected = true;
				}
			}
		}
	}
}

function gappedChanged() {
	var blastForm = document.getElementById("blastForm");
	if (blastForm.gapped.checked) {
		blastForm.gapOpen.enabled = true;
		blastForm.gapExtend.enabled = true;
	} else {
		blastForm.gapOpen.enabled = false;
		blastForm.gapExtend.enabled = false;
	}
}

//---------------------------------------------------------------------
//
//	BlastJob
//
//  arguments:
//    query, db, expect, filter, wordSize, matrix, gapOpen, gapExtend, limit
//  or
//    s (a string containing a concatenation of the above, delimited by '||')

function BlastJob(archived)
{
	this.nr = BlastJob.nextNr++;
	this.query = null;
	this.queryID = null;
	this.program = "blastp";
	this.db = null;
	this.expect = 10.0;
	this.filter = true;
	this.wordSize = 0,
	this.matrix = 'BLOSUM62';
	this.gapped = true;
	this.gapOpen = -1;
	this.gapExtend = -1;
	this.reportLimit = 250;
	this.remoteID = null;
	this.localID = 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
	    var r = Math.random()*16|0, v = c == 'x' ? r : (r&0x3|0x8);
	    return v.toString(16);
	});
	this.status = 'new';
	this.error = null;

	if (archived != null)
	{
		var args = {}, self = $(this);
		$(archived.split('&')).each(function(s,v) {
			var kv = v.split('=');
			self.prop(kv[0], kv[1]);
		});
		
		// default status
		this.status = 'stored';
	}
}

BlastJob.nextNr = 1;
BlastJob.selected = null;

BlastJob.prototype = {
	
	validate: function() {
		var result = false;
		
		if (this.query.length <= this.wordSize) {
			alert("query should be longer than word size (" + this.wordSize + ')');
			return false;
		}
		
		var result = this.query.match(/^>(\S+)( .*)?((\r?\n(.*))+)/);
		if (result != null) {
			this.queryID = result[1];
			this.query = result[3];
		}
	
		this.query = this.query.replace(/\s/g, '');
	
		result = this.query.match(/([^LAGSVETKDPIRNQFYMHCWBZXUO])/i);
		if (result) {
			alert("Query contains invalid characters: '" + result[1] + "'");
			return false;
		}
		
		var valid = validGapValues(this.matrix);
		for (v in valid) {
			if (valid[v][0] == this.gapOpen && valid[v][1] == this.gapExtend) {
				result = true;
				break;
			}
		}
		
		if (result == false)
			alert('Unsupported combination for Matrix and gap open/gap extend values');
		
		return result;
	},
	
	submitted: function(response) {
		this.update();
	},

	setStatus: function(response) {
		this.status = response.status;
	
		if (response.id != null) {
			this.remoteID = response.id;
		}
	
		if (response.queryID != null) {
			this.queryID = response.qid;
		}
		
		if (this.status == 'finished') {
			this.hitCount = response.hitCount;
			this.bestEValue = response.bestEValue;
		}
	
		if (response.error != null) {
			this.error = response.error;
		}
	
		this.update();
	},
	
	update: function() {
		var row = document.getElementById(this.localID);
		if (row == null) {
			throw "Row not found for BlastJob";
		}
	
		$(row.cells[1]).text(this.queryID);
		var className = 'clickable';
		if (BlastJob.selected == this) {
			className += ' selected';
		}
	
		switch (this.status) {
			case 'finished': {
				this.hitCount = this.hitCount;
				this.bestEValue = this.bestEValue;
				
				switch (this.hitCount) {
					case 0: 
						$(row.cells[3]).text('no hits found');
						break;
					case 1:
						$(row.cells[3]).text('1 hit found');
						break;
					default:
						$(row.cells[3]).text(this.hitCount + ' hits found');
						break;
				}
				break;
			}
			case 'error': {
				if (this.error != null) {
					$(row.cells[3]).text(this.error);
				} else {
					$(row.cells[3]).text('error');
				}
				className += ' error';
				break;
			}
			case 'queued': {
				$(row.cells[3]).text('queued');
				break;
			}
			case 'running': {
				$(row.cells[3]).text('running');
				className += ' active';
				break;
			}
			default: {
				$(row.cells[3]).text(this.status);
				break;
			}
		}
		
		row.className = className;
	},
	
	selectJob: function() {
		if (BlastJob.selected != this) {
			if (BlastJob.selected != null) {
				jQuery('#' + BlastJob.selected.localID).removeClass('selected');
			}
			BlastJob.selected = this;
			jQuery('#' + this.localID).addClass('selected');
		}
		
		with (document.getElementById("blastForm")) {
			query.value = '>' + this.queryID + '\n' + this.query.replace(/(.{72})/g, "$1" + '\n');
			program.value = this.program;
			db.value = this.db;
			expect.value = this.expect;
			filter.checked = this.filter;
			wordSize.value = this.wordSize;
			matrix.value = this.matrix;
			gapped.checked = this.gapped;
			gapOpen.value = this.gapOpen;
			gapExtend.value = this.gapExtend;
			reportLimit.value = this.reportLimit;
		}
		
		if (this.result == null) {
			var resultList = document.getElementById('blastResult');
			jQuery(resultList).fadeOut("fast");
	
			this.result = new BlastResult(this);
		} else {
			this.result.updateResultList();
		}
	},
	
	toString: function() {
		return ('query=' + escape(this.query) + '&' +
				'program=' + escape(this.program) + '&' +
				'db=' + escape(this.db) + '&' +
				'expect=' + escape(this.expect) + '&' +
				'filter=' + escape(this.filter ? 'true' : 'false') + '&' +
				'wordSize=' + escape(this.wordSize) + '&' +
				'matrix=' + escape(this.matrix) + '&' +
				'gapped=' + (this.gapped ? 'true' : 'false') + '&' +
				'gapOpen=' + escape(this.gapOpen) + '&' +
				'gapExtend=' + escape(this.gapExtend) + '&' +
				'reportLimit=' + escape(this.reportLimit));
	},
	
	getData: function() {
		return {
			id: this.localID,
			query: this.query,
			program: this.program,
			db: this.db,
			expect: this.expect,
			filter: this.filter,
			wordSize: this.wordSize,
			matrix: this.matrix,
			gapped: this.gapped,
			gapOpen: this.gapOpen,
			gapExtend: this.gapExtend,
			reportLimit: this.reportLimit
		};
	}
}

//---------------------------------------------------------------------
//
//	BlastResult
//

function BlastResult(job) {
	this.job = job;
	this.id = job.remoteID;
	this.fetchHits();
}

BlastResult.colorTable = ['#991F5C', '#5C1F99', '#1F3399', '#1F7099', '#1F9999'];

BlastResult.prototype.fetchHits = function() {
	// fetch the hit list from the server
	if (this.job.status == 'running' || this.job.status == 'queued')
	{
		jQuery("#blastResult").fadeOut();
		return;
	}
	
	var result = this;
	jQuery.getJSON("ajax/blast/result", { job: this.id },
		function(data, status) {
			if (status == "success") 
			{
				if (data.error != null) {
					alert("Retrieving blast results failed:\n" + data.error + "\n\nPlease resubmit this job");
				} else {
					result.hits = data;
					if (BlastJob.selected == result.job) {
						result.updateResultList();
					}
				}
			}
		});
}

BlastResult.prototype.updateResultList = function() {
	var result = this;
	var resultList = document.getElementById('blastResult');
//	resultList.style.display = '';

	jQuery(resultList).fadeOut("fast", function() {
		result.updateResultList2(resultList);
	}).fadeIn("fast");
}

BlastResult.prototype.updateResultList2 = function(resultList) {
	// remove previous results
	while (resultList.tBodies[0].rows.length > 0) {
		resultList.tBodies[0].deleteRow(resultList.tBodies[0].rows.length - 1);
	}
	
	$(resultList.caption).text("Blast results for " + this.job.queryID);
	
	if (this.hits == null) {
		this.fetchHits();
		return;
	}
	
	for (h in this.hits) {
		var hit = this.hits[h];
		hit.job = this.job;	// store
		
		var row = resultList.tBodies[0].insertRow(-1);
		
		row.className = 'clickable';
		row.id = this.job.localID + '.' + hit.nr;
		row.result = this;
		row.hit = hit;
		jQuery(row).click(function() {
			this.result.selectHit(this.hit);
		});
		
		// select checkbox
		var cell = row.insertCell(row.cells.length);
		cell.appendChild(GlobalSelection.createCheckbox(hit.db, hit.doc, hit.seq));

		// Nr
		cell = row.insertCell(row.cells.length);
		$(cell).text(hit.nr);
		cell.className = 'nr';
		
		// ID
		cell = row.insertCell(row.cells.length);
		cell.innerHTML =
			"<a href='link?db=" + escape(hit.db) + "&amp;ix=id&amp;id=" + escape(hit.doc) + "' onclick='doStopPropagation(event);'>" +
			hit.doc + "</a>";
		if (hit.seq.length > 0) {
			cell.innerHTML += '.' + hit.seq;
		}
		
		// coverage
		cell = row.insertCell(row.cells.length);
		cell.width = 100;
		cell.className = 'alignment';
		
		// HTML 5 canvas
		var canvas = document.createElement('canvas');
		if (canvas != null && canvas.getContext != null) {
			cell.appendChild(canvas);
			
			canvas.height = 4;
			canvas.width = 100;
			
			var ctx = canvas.getContext('2d');
			if (ctx != null)
			{
				if (hit.coverage.start > 0)
				{
					ctx.fillStyle = '#CCCCCC';
					ctx.fillRect(0, 0, hit.coverage.start, 4);
				}
				
				ctx.fillStyle = BlastResult.colorTable[hit.coverage.color - 1];
				ctx.fillRect(hit.coverage.start, 0, hit.coverage.start + hit.coverage.length, 4);
				
				if (hit.coverage.start + hit.coverage.length < 100)
				{
					ctx.fillStyle = '#CCCCCC';
					ctx.fillRect(hit.coverage.start + hit.coverage.length, 0, 100, 4);
				}
			}
		}
		
		// Description
		cell = row.insertCell(row.cells.length);
		$(cell).text(hit.desc);

		// Hsps
		cell = row.insertCell(row.cells.length);
		if (typeof hit.hsps == "object") {
			$(cell).text(hit.hsps.length);
		} else {
			$(cell).text(hit.hsps);
		}

		// BitScore
		cell = row.insertCell(row.cells.length);
		$(cell).text(hit.bitScore);

		// Expect
		cell = row.insertCell(row.cells.length);
		$(cell).text(hit.expect.toPrecision(3));
		
		// and reserve some space for the hsps
		row = resultList.tBodies[0].insertRow(-1);
		row.style.display = 'none';
		cell = row.insertCell(row.cells.length);
		cell = row.insertCell(row.cells.length);
		cell.colSpan = 7;
	}
}

BlastResult.prototype.selectHit = function(hit) {
	var row = document.getElementById(this.job.localID + '.' + hit.nr);
	row = row.nextSibling;
	
	if (typeof hit.hsps == "object") {
		if (row.style.display == 'none') {
			this.updateResultHsps(hit, row);
		}
		jQuery(row).fadeToggle("fast");
	} else {
		// fetch the Hsps
		var result = this;
		
		jQuery.getJSON("ajax/blast/result", { job: this.id, hit: hit.nr },
			function(data, status, jqXHR) {
				if (status == "success")
				{
					if (data.error != null) {
						alert("Retrieving blast results failed:\n" + data.error);
					} else {
						hit.hsps = data;
						result.updateResultHsps(hit, row);
						jQuery(row).fadeToggle("fast");
					}
				}
			});
	}
}

BlastResult.prototype.updateResultHsps = function(hit, row) {
	var cell = row.cells[1];
	$(cell).text('');
	
	// create a new table to hold the hsp info
	var table = document.createElement('table');
	table.cellSpacing = 0;
	table.width = '100%';
	cell.appendChild(table);
	
	var tHead = table.createTHead();
	var row = tHead.insertRow(0);
	var th = ['Hsp nr', 'Alignment', 'Score', 'Bitscore', 'E-value', 'Length', 'Identity', 'Similarity', 'Gaps' ];
	for (i in th) {
		var h = document.createElement('th');
		$(h).text(th[i]);
		row.appendChild(h);
	}
	
	var tBody = document.createElement('tbody');
	table.appendChild(tBody);
	
	for (i in hit.hsps) {
		var hsp = hit.hsps[i];
	
		row = table.tBodies[0].insertRow(-1);
		row.id = [this.job.localID, hit.nr, hsp.nr].join('.'); 
		
		row.className = 'clickable';
		row.result = this;
		row.hitNr = hit.nr;
		row.hspNr = hsp.nr;
		jQuery(row).click(function() {
			this.result.selectHsp(this.hitNr, this.hspNr);
		});
		
		// nr
		cell = row.insertCell(row.cells.length);
		$(cell).text(hsp.nr);
		cell.className = 'nr';

		// alignment
		cell = row.insertCell(row.cells.length);
		cell.className = 'alignment';
		
		var canvas = document.createElement('canvas');
		if (canvas != null && canvas.getContext != null) {
			cell.appendChild(canvas);
			
			canvas.height = 8;
			canvas.width = 150;
			
			var ctx = canvas.getContext('2d');
			if (ctx != null)
			{
				var x = hsp.ql[0];
				
				ctx.fillStyle = '#E58AB8';
				ctx.fillRect(x, 0, x + hsp.ql[1], 4);			x += hsp.ql[1];
				ctx.fillStyle = '#991F5A';
				ctx.fillRect(x, 0, x + hsp.ql[2], 4);			x += hsp.ql[2];
				ctx.fillStyle = '#e58AB8';
				ctx.fillRect(x, 0, x + hsp.ql[3], 4);			x += hsp.ql[3];
				if (x < 150) {
					ctx.clearRect(x, 0, 150, 4);
				}
				
				x = hsp.sl[0];
				ctx.fillStyle = '#8AC7E5';
				ctx.fillRect(x, 4, x + hsp.sl[1], 8);			x += hsp.sl[1];
				ctx.fillStyle = '#1F7099';
				ctx.fillRect(x, 4, x + hsp.sl[2], 8);			x += hsp.sl[2];
				ctx.fillStyle = '#8AC7E5';
				ctx.fillRect(x, 4, x + hsp.sl[3], 8);			x += hsp.sl[3];
				if (x < 150) {
					ctx.clearRect(x, 4, 150, 8);
				}
			}
		}
		
		// score
		cell = row.insertCell(row.cells.length);
		cell.style.textAlign = 'right';
		$(cell).text(hsp.score);
		
		// bitscore
		cell = row.insertCell(row.cells.length);
		cell.style.textAlign = 'right';
		$(cell).text(hsp.bitScore);
		
		// e-value
		cell = row.insertCell(row.cells.length);
		cell.style.textAlign = 'right';
		$(cell).text(hsp.expect.toPrecision(3));
		
		// length
		cell = row.insertCell(row.cells.length);
		cell.style.textAlign = 'right';
		$(cell).text(hsp.queryAlignment.length);
		
		// identity
		cell = row.insertCell(row.cells.length);
		cell.style.textAlign = 'right';
		cell.innerHTML = hsp.identity + ' (' +
			Math.floor(100 * hsp.identity / hsp.queryAlignment.length) + '%)';
		
		// similarity
		cell = row.insertCell(row.cells.length);
		cell.style.textAlign = 'right';
		cell.innerHTML = hsp.positive + ' (' +
			Math.floor(100 * hsp.positive / hsp.queryAlignment.length) + '%)';
		
		// gaps
		cell = row.insertCell(row.cells.length);
		cell.style.textAlign = 'right';
		cell.innerHTML = hsp.gaps + ' (' +
			Math.floor(100 * hsp.gaps / hsp.queryAlignment.length) + '%)';
		
		// and some space for the alignment
		row = table.tBodies[0].insertRow(-1);
		row.style.display = 'none';
		
		cell = row.insertCell(row.cells.length);
		cell = row.insertCell(row.cells.length);
		cell.colSpan = 8;
	}
}

BlastResult.prototype.selectHsp = function(hitNr, hspNr) {
	var row = document.getElementById([this.job.localID, hitNr, hspNr].join('.'));
	row = row.nextSibling;
	if (row.style.display == 'none') {

		// calculate alignment, if needed
		
		var hit = this.hits[hitNr - 1];
		var hsp = hit.hsps[hspNr - 1];
		
		if (hsp.alignment == null) {
			this.calculateAlignment(hsp);
		}
		
		row.cells[1].appendChild(hsp.alignment);

		jQuery(row).fadeIn("fast");
	} else {
		jQuery(row).fadeOut("fast");
	}
}

BlastResult.prototype.calculateAlignment = function(hsp) {
	hsp.alignment = 'Hello, world!';

	var qOffset = hsp.queryStart;
	var sOffset = hsp.subjectStart;
	var alignmentLength = hsp.queryAlignment.length;
	
	hsp.alignment = '';
	
	var offset = 0;
	while (offset < alignmentLength) {
       	var stringLength = alignmentLength - offset;
        if (stringLength > 60) {
            stringLength = 60;
        }

        var q = hsp.queryAlignment.substr(offset, stringLength);
        var s = hsp.subjectAlignment.substr(offset, stringLength);

		if (hsp.alignment.length > 1) {
			hsp.alignment += '\n';
		}

        hsp.alignment += "Q: ";
        qOffset += extendAlignment(qOffset, q, hsp);
        hsp.alignment += "           " + hsp.midLine.substr(offset, stringLength) + '\n';
        hsp.alignment += "S: ";
        sOffset += extendAlignment(sOffset, s, hsp);

        offset += stringLength;
    }

	if ($.browser.msie && $.browser.version < 9)
		hsp.alignment = hsp.alignment.replace(/\n/g, '<br/>');
    
    var pre = document.createElement('pre');
    $(pre).html(hsp.alignment);
    hsp.alignment = pre;
}

function extendAlignment(offset, s, hsp) {
	var length = s.replace(/-/g, '').length;
	
	var sf = offset.toString();
	sf = '       '.substr(sf.length) + sf;
	var st = (offset + length - 1).toString();
	st = '       '.substr(st.length) + st;
	
	// low complexity, if any
//	s = s.replace(/([a-z]+)/g, "<span class='masked'>$1</span>");
	var re = /([a-z]+)/;
	var s2 = '';
	while (true) {
		var m = re.exec(s);
		if (m == null)
			break;
		
		s2 += s.substr(0, m.index) + 
			"<span class='masked'>" +
			m[1].toUpperCase() + 
			"</span>";
		s = s.substr(m.index + m[1].length);
	}

	hsp.alignment += sf + ' ' + s2 + s + ' ' + st + '\n';
	
	return length;
}

//---------------------------------------------------------------------
//
//	BlastJobs, a list of blast jobs
//

BlastJobs = {
	jobs: null,
	t: null,
	
	init: function()
	{
		$("p.hideOptions").click(function()
		{
			$(this).hide();
			$("#advancedoptions").show("fast");
		});
		
		if (typeof(localStorage) == 'undefined') {
			if (! mrsCookie.warnedForOlderBrowser) {
				mrsCookie.warnedForOlderBrowser = true;
				mrsCookie.store();

				alert('Your browser does not support HTML5 localStorage. Try upgrading.');
			}
		}

		try {
			localStorage.setItem('mrs-test', 'test');
			localStorage.removeItem('mrs-test');
		} catch (exception) {
			alert('Unable to write to local storage. Do you perhaps have private browsing turned on?');
		}

		BlastJobs.jobs = new Array();

		var storedJobs = unescape(localStorage.getItem('blast-jobs'));
		if (storedJobs != null) {
			storedJobs = storedJobs.split(";;");
			for (var i = 0; i < storedJobs.length; ++i) {
				try {
					var blastJob = new BlastJob(storedJobs[i]);
					if (blastJob.query != null) {
						BlastJobs.jobs.push(blastJob);
					}
				} catch (e) {}
			}
		}
		
		BlastJobs.updateList();
		BlastJobs.poll();
	},
	
	store: function() {
		try {
			if (BlastJobs.jobs != null)
			{
				var s = '';
				for (var i = 0; i < BlastJobs.jobs.length; ++i) {
					s += BlastJobs.jobs[i] +
						"&remoteID=" + BlastJobs.jobs[i].remoteID +
						"&queryID=" + BlastJobs.jobs[i].queryID +
						';;';
				}
				
				localStorage.setItem('blast-jobs', escape(s));
	
				if (BlastJob.selected != null) {
					localStorage.setItem('selectedBlastJob', BlastJob.selected.remoteID);
				} else {
					localStorage.removeItem('selectedBlastJob');
				}
			}
		}
		catch (e) {
			if (!(QUOTA_EXCEEDED_ERR === undefined) && e == QUOTA_EXCEEDED_ERR) {
				alert('Quota exceeded!');
			}
			else throw e;
		}
	},
	
	add: function(job) {
		if (BlastJobs.jobs == null)
			init();
	
		BlastJobs.jobs.push(job);
		BlastJobs.store();
		BlastJobs.updateList();
	},
	
	// fetch a blast job by local-id
	get: function(id) {
		var result;
		for (var i = 0; i < BlastJobs.jobs.length; ++i) {
			if (BlastJobs.jobs[i].localID == id) {
				result = BlastJobs.jobs[i];
				break;
			}
		}
		return result;
	},
	
	// fetch a blast job by remote-id
	getByRemoteID: function(id) {
		var result;
		for (var i = 0; i < BlastJobs.jobs.length; ++i) {
			if (BlastJobs.jobs[i].remoteID == id) {
				result = BlastJobs.jobs[i];
				break;
			}
		}
		return result;
	},
	
	remove: function(id) {
		var result;
		for (var i = 0; i < BlastJobs.jobs.length; ++i) {
			if (BlastJobs.jobs[i].localID == id) {
				BlastJobs.jobs.splice(i, 1);
				
				var list = document.getElementById("jobList");
				var row = document.getElementById(id);
				
				if (list != null && row != null) {
					list.tBodies[0].removeChild(row);
				}
				
				BlastJobs.store();
				break;
			}
		}
		return result;
	},
	
	poll: function() {
		clearTimeout(BlastJobs.t);
	
		var jobstr;
		for (var i = 0; i < BlastJobs.jobs.length; ++i) {
			if (BlastJobs.jobs[i].remoteID == null || BlastJobs.jobs[i].remoteID == "undefined")
				continue;
			if (BlastJobs.jobs[i].status == "new" ||
				BlastJobs.jobs[i].status == "stored" ||
				BlastJobs.jobs[i].status == "queued" ||
				BlastJobs.jobs[i].status == "running")
			{
				if (jobstr == null) {
					jobstr = '';
				} else {
					jobstr = jobstr + ';';
				}
				jobstr = jobstr + BlastJobs.jobs[i].remoteID;
			}
		}
		
		if (jobstr != null) {
			jQuery.post("ajax/blast/status", { jobs: jobstr }, function(data, status) {
					if (status == "success") {
						for (i in data) {
							for (j in BlastJobs.jobs) {
								if (BlastJobs.jobs[j].remoteID == data[i].id) {
									BlastJobs.jobs[j].setStatus(data[i]);
								}
							}
						}
					}
				}, "json");
		}
	
		BlastJobs.t = setTimeout("BlastJobs.poll()", 2500);
	},
	
	updateList: function() {
		if (BlastJobs.jobs.length == 0)
			return;
	
		// update the list in the document
		var list = document.getElementById("jobList");
		if (list != null) {
			if (list.tBodies[0].rows.length == 1 && list.tBodies[0].rows[0].id == "nohits") {
				list.tBodies[0].deleteRow(0);
			}
	
			var newJobs = new Array();
			for (var i = 0; i < BlastJobs.jobs.length; ++i) {
				var row = document.getElementById(BlastJobs.jobs[i].localID);
				if (row == null) {
					newJobs.push(BlastJobs.jobs[i]);
				}
			}
			
			for (var i = 0; i < newJobs.length; ++i) {
				var row = list.tBodies[0].insertRow(-1);
				row.job = newJobs[i];
				jQuery(row).click(function() {
					this.job.selectJob();
					return false;
				});

				with (newJobs[i]) {
					row.id = localID;
					
					// nr
					var cell = row.insertCell(0);
					cell.className = 'nr c1';
					$(cell).text(nr);
					
					// queryID
					cell = row.insertCell(1);
					cell.className = 'c2';
					$(cell).text(queryID);
	
					// db
					cell = row.insertCell(2);
					$(cell).text(db);
					cell.className = 'c3';
	
					// status
					cell = row.insertCell(3);
					cell.className = 'c4';
					
					if (error != null) {
						$(cell).text(error);
					} else {
						$(cell).text(status);
					}
	
					// delete button
					cell = row.insertCell(4);
					
					// work around msie missing features
					if (jQuery.browser.msie == null || jQuery.browser.version > 8) {
						cell.className = 'c5 delete';
					} else {
						cell.className = 'c5';
					}
					
					// apparently, a td is not clickable?
					var img = document.createElement('img');
					img.src = 'images/edit-delete.png';
					
					img.job = newJobs[i];
					jQuery(img).click(function() {
						BlastJobs.deleteJob(this.job);
						return false;
					});
					cell.appendChild(img);
				};
			}
		}
	},

	deleteJob: function(job) {
		if (BlastJob.selected == job) {
			BlastJob.selected = null;
			jQuery("#blastResult").fadeOut();
		}
	
		for (j in BlastJobs.jobs) {
			if (job == BlastJobs.jobs[j]) {
				BlastJobs.jobs.splice(j, 1);
				BlastJobs.store();
				var row = document.getElementById(job.localID);
				if (row != null) {
					document.getElementById('jobList').tBodies[0].removeChild(row);
				}
				break;
			}
		}
	},

	submit: function() {
		try {
			var blastForm = document.getElementById("blastForm");
	
			var job;
			with (blastForm) {
				job = new BlastJob();
				job.query = query.value;
				job.program = program.value;
				job.db = db.value;
				job.expect = expect.value;
				job.filter = filter.checked;
				job.wordSize = wordSize.value;
				job.matrix = matrix.value;
				job.gapped = gapped.checked;
				job.gapOpen = gapOpen.value;
				job.gapExtend = gapExtend.value;
				job.reportLimit = reportLimit.value;
			}
			
			if (job.validate() == false) {
				return;
			}
			
			BlastJobs.add(job);

			jQuery.post("ajax/blast/submit", job.getData(), function(data, status, jqXHR) {
				if (status == "success")
				{
					if (data.error != null) {
						alert(data.error);
						BlastJobs.remove(data.clientId);
					} else {
						var job = BlastJobs.get(data.clientId);
						if (job == null) {
							throw "Response for unknown client id " + data.clientId;
						}
						job.setStatus(data);
						BlastJobs.store();
					}
				}
			}, "json");
			
			BlastJobs.poll();
		}
		catch (e) {
			alert("Submit failed: " + e);
		}
	}
}

addLoadEvent(BlastJobs.init);
addUnloadEvent(BlastJobs.store);
