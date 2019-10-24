//---------------------------------------------------------------------
//
//	JavaScript for the Admin pages
//

class Admin {
	constructor() {
		this.timeout = null;

		document.querySelectorAll("div.nav a")
			.forEach(a => a.addEventListener("click", () => this.selectSection(a.dataset.id)));

		document.querySelectorAll("div.section")
			.forEach(section => {
				section.querySelectorAll("table.select_form tr")
					.forEach(tr => {
						tr.addEventListener("click", () => {
							this.selectForm(section.id, tr.dataset.target);
						});
					})
			});

		document.querySelectorAll("div.delete")
			.forEach(d => d.addEventListener("click", () => {
				const tr = d.parentElement.parentElement;
				tr.remove();
			}))

		document.querySelectorAll("select[name='format']")
			.forEach(s => {
				s.addEventListener("change", () => this.changeFormat(s));

				if (s.nodeValue === 'xml' && s.nextElementSibling !== null)
					s.nextElementSibling.style.display = 'unset';
			})

		document.querySelectorAll('span.add-alias')
			.forEach(addAliasBtn => addAliasBtn.addEventListener("click", (evt) => {
				evt.preventDefault();
				this.addAliasToDb(addAliasBtn.dataset.db);
			})
			);
		
		document.querySelectorAll('button.add-link')
			.forEach(btn => {
				btn.addEventListener("click", (evt) => {
					evt.preventDefault();
					this.addLinkToFormat(btn.dataset.format);
				})
			});

		let v = sessionStorage.getItem('selected-adminpage');
		if (v == null)
			v = 'global';
		this.selectSection(v);

		// Poll the blast queue
		this.poll();
	}

	selectSection(section) {
		document.querySelectorAll("div.section")
			.forEach(d => {
				if (d.id === section)
					d.style.display = 'unset';
				else
					d.style.display = 'none';
			})

		document.querySelectorAll("div.nav a")
			.forEach(a => a.classList.toggle("selected", a.dataset.id === section));

		sessionStorage.setItem('selected-adminpage', section);

		const f = this.selectedForm(section);
		if (f !== '')
			this.selectForm(section, f);
	}

	selectedForm(section) {
		let result = sessionStorage.getItem("selected-" + section);
		if (result == null) {
			const sect = document.querySelector(`#${section} table.select_form td:first-child`);
			result = sect ? sect.textContent : '';
		}
		return result;
	}

	selectForm(section, id) {
		const sect = document.getElementById(section);

		sect.querySelectorAll("form.admin_form").forEach(f => f.style.display = 'none');
		sect.querySelector(`form[data-id=${id}`).style.display = 'unset';

		sect.querySelectorAll("table.select_form tr")
			.forEach(tr => tr.classList.toggle('selected', tr.textContent === id));

		const btns = sect.querySelectorAll("form.add-del input[name='selected']");
		if (btns != null)
			btns.forEach(btn => btn.nodeValue = id);

		try {
			sessionStorage.setItem(`selected-${section}`, id);
		} catch (e) { }
	}

	changeFormat(select) {
		const db = select.dataset.db;
		const form = document.querySelector(`form[data-id=db-${db}]`);
		const selected = select.options[select.selectedIndex].value;

		const stylesheet = form.querySelector("input[name='stylesheet']");

		if (selected == 'xml')
			stylesheet.style.display = '';
		else
			stylesheet.style.display = 'none';
	}

	addLinkToFormat(formatID) {
		const table = document.getElementById(`format-link-table-${formatID}`);
		const lastRow = table.querySelector("tr:last-child");
		const newRow = lastRow.cloneNode(true);

		newRow.querySelector("div.delete")
			.addEventListener("click", () => table.removeChild(newRow));

		table.insertBefore(newRow, lastRow);

		newRow.style.display = '';
		table.style.display = '';
	}

	addAliasToDb(db) {
		const table = document.querySelector(`table[data-id="${db}-aliases"]`);
		const lastRow = table.querySelector("tr:last-child");
		const newRow = lastRow.cloneNode(true);

		newRow.querySelector("div.delete")
			.addEventListener("click", () => table.removeChild(newRow));

		table.insertBefore(newRow, lastRow);

		newRow.style.display = '';
		table.style.display = '';
	}

	// Blast Queue management

	poll() {
		if (this.timeout != null)
			clearTimeout(this.timeout);
		this.timeout = null;

		fetch("ajax/blast/queue")
			.then(response => {
				if (response.ok)
					return response.json();
				throw "Retrieving blast queue failed";
			})
			.then(data => {
				if (data.error != null)
					throw "Retrieving blast queue failed:\n" + data.error;
				this.updateBlastQueue(data);
			})
			.catch(err => alert(err));

		this.timeout = setTimeout(() => this.poll(), 10000);
	}

	updateBlastQueue(data) {
		const table = document.getElementById("blastQueue");

		// remove previous results
		while (table.tBodies[0].rows.length > 0) {
			table.tBodies[0].deleteRow(table.tBodies[0].rows.length - 1);
		}

		for (let ix in data) {
			const job = data[ix];
			const row = table.tBodies[0].insertRow(-1);

			row.classList.add('clickable');
			row.id = job.id;

			// ID
			let cell = row.insertCell(row.cells.length);
			cell.textContent = job.id;
			cell.classList.add('jobID');

			// query length
			cell = row.insertCell(row.cells.length);
			cell.textContent = job.queryLength;
			cell.style.textAlign = 'right';
			cell.classList.add('nr');

			// databank
			cell = row.insertCell(row.cells.length);
			cell.textContent = job.db;

			// status
			cell = row.insertCell(row.cells.length);
			cell.textContent = job.status;

			cell.classList.toggle('active', job.status === 'running');
			cell.classList.toggle('scheduled', job.status === 'queued');

			// remove link
			cell = row.insertCell(4);
			cell.classList.add('c5', 'delete');

			// apparently, a td is not clickable?
			const img = document.createElement('img');
			img.src = 'images/edit-delete.png';
			img.jobId = job.id;
			img.addEventListener('click', () => this.deleteJob(job.id));
			cell.appendChild(img);
		}
	}

	deleteJob(id) {
		fetch(`ajax/blast/delete?job=${id}`)
			.then(response => {
				if (response.ok)
					return response.json();
				throw "Deleting blast job failed";
			})
			.then(data => {
				if (data.error)
					throw "Deleting blast job failed:\n" + data.error;
				this.poll();
			})
			.catch(err => alert(err));
	}
};

window.addEventListener("load", () => {
	new Admin();
});
